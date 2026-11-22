// Pegasus Frontend
// Copyright (C) 2017-2020  Mátyás Mustoha
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.


#include "GogGamelist.h"

#include "Log.h"
#include "Paths.h"
#include "model/gaming/Collection.h"
#include "model/gaming/Game.h"
#include "providers/SearchContext.h"
#include "utils/CommandTokenizer.h"
#include "utils/MoveOnly.h"
#include "utils/PathTools.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTextStream>

#include <algorithm>


namespace {
// TODO: C++20
struct GogEntry {
    QString id;
    QString name;
    QString exe;
    QString launch_cmd;
    QString workdir;

    GogEntry(QString id, QString name, QString exe, QString launch_cmd, QString workdir)
        : id(std::move(id))
        , name(std::move(name))
        , exe(std::move(exe))
        , launch_cmd(std::move(launch_cmd))
        , workdir(std::move(workdir))
    {}

    MOVE_ONLY(GogEntry)
};

bool is_heroic_launch(const QString& launch_cmd)
{
    return launch_cmd.contains(QLatin1String("heroic://"));
}

bool invalid_entry(const GogEntry& entry)
{
    if (entry.name.isEmpty() || entry.launch_cmd.isEmpty() || entry.exe.isEmpty())
        return true;

    // Heroic entries may point at an unmounted multi-drive install_path; still list them.
    if (is_heroic_launch(entry.launch_cmd))
        return false;

    return !QFileInfo::exists(entry.exe);
}

#ifdef Q_OS_LINUX
constexpr QLatin1String HEROIC_FLATPAK_PKG("com.heroicgameslauncher.hgl");

QString normalize_install_path(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

bool json_truthy(const QJsonValue& value)
{
    if (value.isBool())
        return value.toBool(false);
    if (value.isDouble())
        return value.toInt(0) != 0;
    if (value.isString()) {
        const QString s = value.toString().trimmed().toLower();
        return s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes");
    }
    return false;
}

QString wrap_clean_env(const QString& command)
{
    // Drop AppImage/Qt library overrides so Heroic/Proton use the host toolchain.
    return QStringLiteral(
               "env -u LD_LIBRARY_PATH -u LD_PRELOAD -u QT_PLUGIN_PATH "
               "-u QT_QPA_PLATFORM_PLUGIN_PATH -u APPDIR -u APPIMAGE -u ARGV0 "
               "-u PYTHONHOME -u PYTHONPATH -u PERLLIB ")
        % command;
}

QStringList appimage_search_roots()
{
    QStringList roots {
        paths::homePath() + QStringLiteral("/Applications"),
        paths::homePath() + QStringLiteral("/AppImages"),
        paths::homePath() + QStringLiteral("/bin"),
        paths::homePath() + QStringLiteral("/.local/bin"),
        QStringLiteral("/opt"),
        paths::homePath() + QStringLiteral("/Downloads"),
        paths::homePath() + QStringLiteral("/Desktop"),
    };
    roots.removeDuplicates();
    return roots;
}

bool name_looks_like_heroic(const QString& filename)
{
    const QString lower = filename.toLower();
    return lower.contains(QLatin1String("heroic"))
        || lower.contains(QLatin1String("dark1hgl"))
        || lower.contains(QLatin1String("dark1-hgl"));
}

QString find_heroic_appimage()
{
    for (const QString& root : appimage_search_roots()) {
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QStringList files = dir.entryList(QStringList { QStringLiteral("*.AppImage") },
                                                QDir::Files | QDir::Readable);
        for (const QString& file : files) {
            if (!name_looks_like_heroic(file))
                continue;
            const QString path = dir.absoluteFilePath(file);
            if (QFileInfo(path).isExecutable())
                return path;
        }
    }
    return {};
}

QString find_heroic_desktop_exec()
{
    const QStringList desktop_dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString& dir_path : desktop_dirs) {
        QDirIterator it(dir_path, QStringList { QStringLiteral("*.desktop") }, QDir::Files);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString base = it.fileName().toLower();
            if (!name_looks_like_heroic(base))
                continue;

            QFile file(path);
            if (!file.open(QFile::ReadOnly | QFile::Text))
                continue;

            QTextStream stream(&file);
            stream.setCodec("UTF-8");
            QString line;
            while (stream.readLineInto(&line)) {
                if (!line.startsWith(QLatin1String("Exec=")))
                    continue;
                QString exec = line.mid(5).trimmed();
                // Strip desktop field codes and trailing args noise; keep the binary path.
                exec.replace(QRegularExpression(QStringLiteral("%[fFuUdDnNickvm]")), QString());
                const QStringList parts = ::utils::tokenize_command(exec);
                if (parts.isEmpty())
                    break;
                const QString candidate = parts.front();
                if (QFileInfo::exists(candidate) || !QStandardPaths::findExecutable(candidate).isEmpty())
                    return candidate.contains(QLatin1Char('/'))
                        ? candidate
                        : QStandardPaths::findExecutable(candidate);
            }
        }
    }
    return {};
}

QString find_heroic_launch_prefix(const HashMap<QString, std::vector<QString>>& options, const QString& log_tag)
{
    const auto helper_it = options.find(QLatin1String("launch_helper"));
    if (helper_it != options.cend() && !helper_it->second.empty() && !helper_it->second.front().isEmpty())
        return ::utils::escape_command(helper_it->second.front());

    const QStringList candidates {
        QStringLiteral("heroic"),
        QStringLiteral("dark1hgl"),
        QStringLiteral("Dark1HGL"),
        QStringLiteral("Heroic"),
    };
    for (const QString& name : candidates) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return ::utils::escape_command(found);
    }

    const QString desktop_exec = find_heroic_desktop_exec();
    if (!desktop_exec.isEmpty())
        return ::utils::escape_command(desktop_exec);

    const QString appimage = find_heroic_appimage();
    if (!appimage.isEmpty())
        return ::utils::escape_command(appimage);

    // Prefer Flatpak only after native/AppImage lookups fail.
    const QString flatpak_cfg = paths::homePath()
        % QLatin1String("/.var/app/")
        % HEROIC_FLATPAK_PKG
        % QLatin1String("/config/heroic/");
    const QString flatpak_bin = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if (QFileInfo::exists(flatpak_cfg) && !flatpak_bin.isEmpty())
        return QLatin1String("flatpak run ") % HEROIC_FLATPAK_PKG;

    Log::warning(log_tag, LOGMSG(
        "Heroic/Dark1HGL launcher not found on PATH, desktop entries, common AppImage dirs, or Flatpak. "
        "Set providers.gog.launch_helper to your Heroic/Dark1HGL binary or AppImage. "
        "Skipping Heroic GOG import."));
    return {};
}

QStringList heroic_config_roots()
{
    QStringList roots;

    const QStringList config_roots = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    for (const QString& root : config_roots) {
        roots << (root + QStringLiteral("/heroic"));
        roots << (root + QStringLiteral("/Dark1HGL"));
        roots << (root + QStringLiteral("/dark1hgl"));
    }

    roots << (paths::homePath()
        % QLatin1String("/.var/app/")
        % HEROIC_FLATPAK_PKG
        % QLatin1String("/config/heroic"));

    roots.removeDuplicates();
    return roots;
}

// Shallow only: install root + one directory level. Avoid walking Proton/Wine trees (#10).
QString read_goggame_info_name(const QString& install_path, const QString& preferred_id, QString* o_id)
{
    QFileInfoList info_files;

    const QDir root(install_path);
    info_files += root.entryInfoList(QStringList { QStringLiteral("goggame-*.info") }, QDir::Files);

    const QFileInfoList subdirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& subdir : subdirs) {
        // Skip obvious Proton/Wine noise directories.
        const QString base = subdir.fileName();
        if (base.compare(QLatin1String("dosdevices"), Qt::CaseInsensitive) == 0
            || base.compare(QLatin1String("drive_c"), Qt::CaseInsensitive) == 0
            || base.compare(QLatin1String("pfx"), Qt::CaseInsensitive) == 0
            || base.startsWith(QLatin1Char('.')))
            continue;

        info_files += QDir(subdir.absoluteFilePath())
                          .entryInfoList(QStringList { QStringLiteral("goggame-*.info") }, QDir::Files);
    }

    if (info_files.isEmpty())
        return {};

    auto parse_info = [](const QFileInfo& finfo, QString* name_out, QString* id_out) -> bool {
        QFile file(finfo.absoluteFilePath());
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return false;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            return false;
        const QJsonObject obj = doc.object();
        *name_out = obj.value(QLatin1String("name")).toString();
        *id_out = obj.value(QLatin1String("gameId")).toString();
        if (id_out->isEmpty())
            *id_out = obj.value(QLatin1String("rootGameId")).toString();
        return !name_out->isEmpty() || !id_out->isEmpty();
    };

    // Prefer goggame-<preferred_id>.info when available.
    if (!preferred_id.isEmpty()) {
        for (const QFileInfo& finfo : info_files) {
            if (finfo.completeBaseName() == QLatin1String("goggame-") + preferred_id
                || finfo.fileName() == QLatin1String("goggame-") + preferred_id + QLatin1String(".info")) {
                QString name;
                QString id;
                if (parse_info(finfo, &name, &id)) {
                    if (o_id && o_id->isEmpty() && !id.isEmpty())
                        *o_id = id;
                    if (!name.isEmpty())
                        return name;
                }
            }
        }
    }

    // Prefer root-level infos over nested; prefer gameId == preferred_id.
    std::sort(info_files.begin(), info_files.end(), [&install_path](const QFileInfo& a, const QFileInfo& b) {
        const bool a_root = QFileInfo(a.absolutePath()) == QFileInfo(install_path);
        const bool b_root = QFileInfo(b.absolutePath()) == QFileInfo(install_path);
        if (a_root != b_root)
            return a_root;
        return a.fileName() < b.fileName();
    });

    QString fallback_name;
    for (const QFileInfo& finfo : info_files) {
        QString name;
        QString id;
        if (!parse_info(finfo, &name, &id))
            continue;

        if (!preferred_id.isEmpty() && id == preferred_id) {
            if (o_id && o_id->isEmpty())
                *o_id = id;
            if (!name.isEmpty())
                return name;
        }

        if (fallback_name.isEmpty() && !name.isEmpty()) {
            fallback_name = name;
            if (o_id && o_id->isEmpty() && !id.isEmpty())
                *o_id = id;
        }
    }

    return fallback_name;
}

QString build_heroic_launch_cmd(const QString& launch_prefix, const QString& app_name)
{
    const QString raw = launch_prefix
        % QLatin1String(" --no-gui heroic://launch/gog/")
        % app_name;
    return wrap_clean_env(raw);
}

void append_heroic_entries(
    std::vector<GogEntry>& entries,
    const HashMap<QString, std::vector<QString>>& options,
    const QString& log_tag)
{
    const QString launch_prefix = find_heroic_launch_prefix(options, log_tag);
    if (launch_prefix.isEmpty())
        return;

    const QStringList roots = heroic_config_roots();
    QSet<QString> seen_keys;

    for (const QString& root : roots) {
        const QString installed_path = root + QStringLiteral("/gog_store/installed.json");
        QFile installed_file(installed_path);
        if (!installed_file.open(QFile::ReadOnly | QFile::Text))
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(installed_file.readAll());
        QJsonArray installed;
        if (doc.isObject())
            installed = doc.object().value(QLatin1String("installed")).toArray();
        else if (doc.isArray())
            installed = doc.array();
        else
            continue;

        for (const QJsonValue& value : installed) {
            const QJsonObject obj = value.toObject();
            if (obj.isEmpty())
                continue;
            if (json_truthy(obj.value(QLatin1String("is_dlc"))))
                continue;

            const QString app_name = obj.value(QLatin1String("appName")).toString();
            QString install_path = normalize_install_path(obj.value(QLatin1String("install_path")).toString());
            if (app_name.isEmpty() || install_path.isEmpty())
                continue;

            const QString dedupe_key = app_name % QChar('|') % install_path;
            if (seen_keys.contains(dedupe_key))
                continue;
            seen_keys.insert(dedupe_key);

            const bool path_missing = !QFileInfo::exists(install_path);
            if (path_missing) {
                Log::warning(log_tag, LOGMSG(
                    "Heroic GOG install for `%1` is missing at `%2` (drive unmounted?). "
                    "Keeping the entry; launch may fail until the path is available.")
                    .arg(app_name, install_path));
            }

            QString id = app_name;
            QString name;
            if (!path_missing)
                name = read_goggame_info_name(install_path, app_name, &id);
            if (name.isEmpty())
                name = QFileInfo(install_path).fileName();
            if (name.isEmpty())
                name = app_name;

            QString exe = install_path;
            const QString relative_exe = normalize_install_path(obj.value(QLatin1String("executable")).toString());
            if (!path_missing && !relative_exe.isEmpty()) {
                const QString candidate = install_path + QChar('/') + relative_exe;
                if (QFileInfo::exists(candidate))
                    exe = candidate;
            }
            // Stable existing path for unmounted installs so registration still has an identity.
            if (path_missing)
                exe = installed_path;

            entries.emplace_back(
                std::move(id),
                std::move(name),
                std::move(exe),
                build_heroic_launch_cmd(launch_prefix, app_name),
                install_path);
        }
    }
}

void dedupe_entries_by_gog_id(std::vector<GogEntry>& entries)
{
    // One entry per GOG id. Prefer Heroic launches (Proton path) over native start.sh.
    HashMap<QString, size_t> id_index;
    std::vector<GogEntry> out;
    out.reserve(entries.size());

    for (GogEntry& entry : entries) {
        if (entry.id.isEmpty()) {
            out.emplace_back(std::move(entry));
            continue;
        }

        const auto it = id_index.find(entry.id);
        if (it == id_index.end()) {
            id_index.emplace(entry.id, out.size());
            out.emplace_back(std::move(entry));
            continue;
        }

        GogEntry& existing = out[it->second];
        const bool entry_heroic = is_heroic_launch(entry.launch_cmd);
        const bool existing_heroic = is_heroic_launch(existing.launch_cmd);
        if (entry_heroic && !existing_heroic) {
            existing = std::move(entry);
        }
        // else keep existing (already Heroic, or both native)
    }

    entries = std::move(out);
}
#endif // Q_OS_LINUX

std::vector<GogEntry> find_game_entries(
    const HashMap<QString, std::vector<QString>>& options,
    const QString& log_tag)
{
    std::vector<GogEntry> entries;

#ifdef Q_OS_WIN
    Q_UNUSED(options);
    Q_UNUSED(log_tag);

    QSettings reg_base(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\GOG.com\\Games"),
                       QSettings::NativeFormat);

    const QStringList reg_games = reg_base.childGroups();
    for (const QString& reg_game : reg_games) {
        reg_base.beginGroup(reg_game);

        entries.emplace_back(
            reg_base.value(QStringLiteral("gameID")).toString(),
            reg_base.value(QStringLiteral("gameName")).toString(),
            reg_base.value(QStringLiteral("exe")).toString(),
            ::utils::escape_command(reg_base.value(QStringLiteral("launchCommand")).toString()),
            reg_base.value(QStringLiteral("workingDir")).toString()
        );

        reg_base.endGroup();
    }
#endif

#ifdef Q_OS_LINUX
    const std::vector<QString> directories = [&options]{
        std::vector<QString> out {
            paths::homePath() + QStringLiteral("/GOG Games"),
        };
        const auto directories_it = options.find(QLatin1String("directories"));
        if (directories_it != options.cend())
            out.insert(out.end(), directories_it->second.cbegin(), directories_it->second.cend());

        return out;
    }();

    constexpr auto dir_filters = QDir::Dirs | QDir::NoDotAndDotDot;
    constexpr auto dir_flags = QDirIterator::FollowSymlinks;
    const QRegularExpression re_numeric(QStringLiteral("^\\d+$"));

    for (const QString& root : directories) {
        QDirIterator dir_it(root, dir_filters, dir_flags);
        while (dir_it.hasNext()) {
            const QString gamedir(dir_it.next());

            const QString launcher_path(gamedir + QStringLiteral("/start.sh"));
            const QFileInfo launcher_file(launcher_path);
            if (!launcher_file.exists() || !launcher_file.isFile() || !launcher_file.isExecutable())
                continue;

            GogEntry entry {
                QString(),
                dir_it.fileName(),
                launcher_path,
                ::utils::escape_command(launcher_path),
                dir_it.filePath(),
            };

            const QString gameinfo_path(gamedir + QStringLiteral("/gameinfo"));
            QFile config_file(gameinfo_path);
            if (config_file.open(QFile::ReadOnly | QFile::Text)) {
                QTextStream stream(&config_file);
                stream.setCodec("UTF-8");

                QString line;
                unsigned short lineno = 0;
                while (stream.readLineInto(&line, 256) && lineno <= 3) {
                    if (lineno == 0 && !line.isEmpty())
                        entry.name = line;

                    if (lineno == 3 && re_numeric.match(line).hasMatch())
                        entry.id = line;

                    lineno++;
                }
            }

            entries.emplace_back(std::move(entry));
        }
    }

    append_heroic_entries(entries, options, log_tag);
    dedupe_entries_by_gog_id(entries);
#else
    Q_UNUSED(log_tag);
#endif


    entries.erase(std::remove_if(entries.begin(), entries.end(), invalid_entry), entries.end());
    return entries;
}

HashMap<QString, model::Game*> register_game_entries(
    const std::vector<GogEntry>& gogentries,
    model::Collection& collection,
    providers::SearchContext& sctx)
{
    // NOTE: Games without known GOG ID will be present in SCTX, but not in this output map
    HashMap<QString, model::Game*> gogid_map;

    for (const GogEntry& gogentry : gogentries) {
        QFileInfo finfo(gogentry.exe);

        QString path = ::clean_abs_path(finfo);
        model::Game* game_ptr = sctx.game_by_filepath(path);
        if (!game_ptr) {
            game_ptr = sctx.create_game_for(collection);
            sctx.game_add_filepath(*game_ptr, std::move(path));
        }

        (*game_ptr)
            .setTitle(gogentry.name)
            .setLaunchCmd(gogentry.launch_cmd)
            .setLaunchWorkdir(gogentry.workdir);

        if (!gogentry.id.isEmpty())
            gogid_map.emplace(gogentry.id, game_ptr);
    }

    return gogid_map;
}
} // namespace


namespace providers {
namespace gog {

Gamelist::Gamelist(QString log_tag)
    : m_log_tag(std::move(log_tag))
{}

HashMap<QString, model::Game*> Gamelist::find(
    const HashMap<QString, std::vector<QString>>& options,
    model::Collection& collection,
    providers::SearchContext& sctx) const
{
    const std::vector<GogEntry> gogentries = find_game_entries(options, m_log_tag);
    Log::info(m_log_tag, LOGMSG("%1 games found").arg(QString::number(gogentries.size())));

    return register_game_entries(gogentries, collection, sctx);
}

} // namespace steam
} // namespace providers
