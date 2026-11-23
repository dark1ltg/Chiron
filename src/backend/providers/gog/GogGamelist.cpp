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
#include <QUrl>

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

struct HeroicLaunchers {
    QString native_prefix;   // escaped path/command for native or AppImage helper
    QString flatpak_prefix;  // "flatpak run com.heroicgameslauncher.hgl" or empty
};

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

bool looks_numeric_gog_id(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("^\\d+$"));
    return re.match(value).hasMatch();
}

bool is_wrapper_token(const QString& token)
{
    const QString base = QFileInfo(token).fileName().toLower();
    return base == QLatin1String("env")
        || base == QLatin1String("sh")
        || base == QLatin1String("bash")
        || base == QLatin1String("zsh")
        || base == QLatin1String("dash")
        || base == QLatin1String("nice")
        || base == QLatin1String("ionice")
        || base == QLatin1String("systemd-run")
        || base == QLatin1String("dbus-launch");
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

bool name_looks_like_dark1hgl(const QString& filename)
{
    const QString lower = filename.toLower();
    // Dark1HGL ships as Heroic-*-gog.<n>-*.AppImage as well as dark1hgl-named builds.
    return lower.contains(QLatin1String("dark1hgl"))
        || lower.contains(QLatin1String("dark1-hgl"))
        || lower.contains(QLatin1String("-gog."))
        || lower.contains(QLatin1String("_gog."));
}

bool is_appimage_path(const QString& path_or_name)
{
    return QFileInfo(path_or_name).fileName().endsWith(QLatin1String(".AppImage"), Qt::CaseInsensitive);
}

int score_heroic_candidate(const QString& path_or_name)
{
    const QString lower = QFileInfo(path_or_name).fileName().toLower();
    int score = 0;
    if (name_looks_like_dark1hgl(lower))
        score += 1000;
    else if (lower.contains(QLatin1String("heroic")))
        score += 100;
    else
        return 0;

    // Prefer AppImages over distro/native packages so Chiron and Dark1HGL share one launcher.
    if (is_appimage_path(lower))
        score += 500;

    // Prefer newer Dark1HGL/Heroic-*-gog.N builds when several AppImages exist.
    static const QRegularExpression ver_re(
        QStringLiteral(R"(heroic-(\d+)\.(\d+)\.(\d+).*?gog\.(\d+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = ver_re.match(lower);
    if (m.hasMatch()) {
        score += m.captured(1).toInt() * 100000
            + m.captured(2).toInt() * 1000
            + m.captured(3).toInt() * 10
            + m.captured(4).toInt();
    }

    const QFileInfo finfo(path_or_name);
    if (finfo.exists())
        score += static_cast<int>(std::min<qint64>(finfo.lastModified().toSecsSinceEpoch() % 1000, 999));
    return score;
}

QString resolve_helper_path(const QString& candidate)
{
    if (candidate.isEmpty())
        return {};
    if (candidate.contains(QLatin1Char('/'))) {
        const QFileInfo finfo(candidate);
        if (finfo.exists() && finfo.isExecutable())
            return finfo.absoluteFilePath();
        return {};
    }
    return QStandardPaths::findExecutable(candidate);
}

QString find_heroic_appimage()
{
    QString best;
    int best_score = 0;

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
            if (!QFileInfo(path).isExecutable())
                continue;
            const int score = score_heroic_candidate(path);
            if (score > best_score) {
                best_score = score;
                best = path;
            }
        }
    }
    return best;
}

QString find_heroic_desktop_exec()
{
    QString best;
    int best_score = 0;

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
                exec.replace(QRegularExpression(QStringLiteral("%[fFuUdDnNickvm]")), QString());
                const QStringList parts = ::utils::tokenize_command(exec);

                QString chosen;
                for (const QString& part : parts) {
                    if (part.startsWith(QLatin1Char('-')))
                        continue;
                    if (is_wrapper_token(part))
                        continue;
                    if (!name_looks_like_heroic(part) && !name_looks_like_heroic(QFileInfo(part).fileName()))
                        continue;
                    const QString resolved = resolve_helper_path(part);
                    if (!resolved.isEmpty()) {
                        chosen = resolved;
                        break;
                    }
                }
                if (chosen.isEmpty())
                    break;

                const int score = score_heroic_candidate(chosen) + score_heroic_candidate(base);
                if (score > best_score) {
                    best_score = score;
                    best = chosen;
                }
                break;
            }
        }
    }
    return best;
}

bool is_flatpak_heroic_root(const QString& root)
{
    return root.contains(QLatin1String("/.var/app/") + HEROIC_FLATPAK_PKG);
}

HeroicLaunchers find_heroic_launchers(const HashMap<QString, std::vector<QString>>& options, const QString& log_tag)
{
    HeroicLaunchers out;

    const auto helper_it = options.find(QLatin1String("launch_helper"));
    if (helper_it != options.cend() && !helper_it->second.empty() && !helper_it->second.front().isEmpty()) {
        const QString raw = helper_it->second.front();
        const QString resolved = resolve_helper_path(raw);
        if (!resolved.isEmpty()) {
            out.native_prefix = ::utils::escape_command(resolved);
            Log::info(log_tag, LOGMSG("Using providers.gog.launch_helper: `%1`").arg(resolved));
        } else {
            Log::warning(log_tag, LOGMSG(
                "`providers.gog.launch_helper` is set to `%1` but that path is missing or not executable; ignoring.")
                .arg(raw));
        }
    }

    // Prefer a Dark1HGL / Heroic-*-gog.* AppImage over system-packaged Heroic so
    // Chiron launches through the same binary (and config) the user actually uses.
    if (out.native_prefix.isEmpty()) {
        const QString appimage = find_heroic_appimage();
        if (!appimage.isEmpty()) {
            out.native_prefix = ::utils::escape_command(appimage);
            Log::info(log_tag, LOGMSG("Using Dark1HGL/Heroic AppImage: `%1`").arg(appimage));
        }
    }

    if (out.native_prefix.isEmpty()) {
        // PATH: Dark1HGL names only at this stage — avoid grabbing stock `heroic` early.
        const QStringList candidates {
            QStringLiteral("dark1hgl"),
            QStringLiteral("Dark1HGL"),
        };
        for (const QString& name : candidates) {
            const QString found = QStandardPaths::findExecutable(name);
            if (found.isEmpty())
                continue;
            out.native_prefix = ::utils::escape_command(found);
            Log::info(log_tag, LOGMSG("Using Dark1HGL from PATH: `%1`").arg(found));
            break;
        }
    }

    if (out.native_prefix.isEmpty()) {
        const QString desktop_exec = find_heroic_desktop_exec();
        if (!desktop_exec.isEmpty()) {
            out.native_prefix = ::utils::escape_command(desktop_exec);
            Log::info(log_tag, LOGMSG("Using Heroic/Dark1HGL from desktop entry: `%1`").arg(desktop_exec));
        }
    }

    if (out.native_prefix.isEmpty()) {
        // Last resort: stock system Heroic package.
        for (const QString& name : { QStringLiteral("heroic"), QStringLiteral("Heroic") }) {
            const QString found = QStandardPaths::findExecutable(name);
            if (found.isEmpty())
                continue;
            out.native_prefix = ::utils::escape_command(found);
            Log::info(log_tag, LOGMSG(
                "No Dark1HGL AppImage found; falling back to system Heroic: `%1`").arg(found));
            break;
        }
    }

    const QString flatpak_cfg = paths::homePath()
        % QLatin1String("/.var/app/")
        % HEROIC_FLATPAK_PKG
        % QLatin1String("/config/heroic/");
    const QString flatpak_bin = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if (QFileInfo::exists(flatpak_cfg) && !flatpak_bin.isEmpty())
        out.flatpak_prefix = QLatin1String("flatpak run ") % HEROIC_FLATPAK_PKG;

    if (out.native_prefix.isEmpty() && out.flatpak_prefix.isEmpty()) {
        Log::warning(log_tag, LOGMSG(
            "Dark1HGL/Heroic AppImage not found in ~/Applications, ~/AppImages, ~/Downloads, "
            "~/Desktop, ~/.local/bin, ~/bin, or /opt (look for Heroic-*-gog.*.AppImage). "
            "Set providers.gog.launch_helper to your Dark1HGL AppImage path. "
            "Skipping Heroic GOG import."));
    }

    return out;
}

QString launch_prefix_for_root(const HeroicLaunchers& launchers, const QString& root)
{
    if (is_flatpak_heroic_root(root)) {
        if (!launchers.flatpak_prefix.isEmpty())
            return launchers.flatpak_prefix;
        return launchers.native_prefix;
    }
    if (!launchers.native_prefix.isEmpty())
        return launchers.native_prefix;
    return launchers.flatpak_prefix;
}

QStringList heroic_config_roots()
{
    // Prefer Dark1HGL config first so library/launch match the Dark1HGL AppImage.
    QStringList roots;

    const QStringList config_roots = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    for (const QString& root : config_roots) {
        roots << (root + QStringLiteral("/Dark1HGL"));
        roots << (root + QStringLiteral("/dark1hgl"));
    }
    for (const QString& root : config_roots)
        roots << (root + QStringLiteral("/heroic"));

    roots << (paths::homePath()
        % QLatin1String("/.var/app/")
        % HEROIC_FLATPAK_PKG
        % QLatin1String("/config/heroic"));

    roots.removeDuplicates();
    return roots;
}

// Shallow only: install root + one directory level. Avoid walking Proton/Wine trees.
QString read_goggame_info_name(const QString& install_path, const QString& preferred_id, QString* o_id)
{
    QFileInfoList info_files;

    const QDir root(install_path);
    info_files += root.entryInfoList(QStringList { QStringLiteral("goggame-*.info") }, QDir::Files);

    const QFileInfoList subdirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& subdir : subdirs) {
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

    auto maybe_set_id = [&](const QString& id) {
        if (!o_id || id.isEmpty())
            return;
        // Prefer numeric GOG product ids for metadata API lookups.
        if (o_id->isEmpty() || (!looks_numeric_gog_id(*o_id) && looks_numeric_gog_id(id)))
            *o_id = id;
    };

    if (!preferred_id.isEmpty()) {
        for (const QFileInfo& finfo : info_files) {
            if (finfo.completeBaseName() == QLatin1String("goggame-") + preferred_id
                || finfo.fileName() == QLatin1String("goggame-") + preferred_id + QLatin1String(".info")) {
                QString name;
                QString id;
                if (parse_info(finfo, &name, &id)) {
                    maybe_set_id(id);
                    if (!name.isEmpty())
                        return name;
                }
            }
        }
    }

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
            maybe_set_id(id);
            if (!name.isEmpty())
                return name;
        }

        if (fallback_name.isEmpty() && !name.isEmpty()) {
            fallback_name = name;
            maybe_set_id(id);
        } else if (looks_numeric_gog_id(id)) {
            maybe_set_id(id);
        }
    }

    return fallback_name;
}

QString build_heroic_launch_cmd(const QString& launch_prefix, const QString& app_name)
{
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(app_name));
    // gui=false is honored by Heroic/Dark1HGL protocol handling even when a
    // second instance only forwards argv to an already-running Dark1HGL.
    const QString url = QLatin1String("heroic://launch/gog/")
        % encoded
        % QLatin1String("?gui=false");

    // Nested AppImage→AppImage launches (Chiron AppImage starting Dark1HGL
    // AppImage) often fail FUSE mounts; extract-and-run avoids a silent no-op.
    const bool helper_is_appimage = launch_prefix.contains(QLatin1String(".AppImage"), Qt::CaseInsensitive);
    const QString flags = helper_is_appimage
        ? QLatin1String(" --appimage-extract-and-run --no-gui ")
        : QLatin1String(" --no-gui ");

    // Env sanitization is done in ProcessLauncher for the child process.
    return launch_prefix % flags % url;
}

void append_heroic_entries(
    std::vector<GogEntry>& entries,
    const HashMap<QString, std::vector<QString>>& options,
    const QString& log_tag)
{
    const HeroicLaunchers launchers = find_heroic_launchers(options, log_tag);
    if (launchers.native_prefix.isEmpty() && launchers.flatpak_prefix.isEmpty())
        return;

    const QStringList roots = heroic_config_roots();
    QSet<QString> seen_keys;

    for (const QString& root : roots) {
        const QString launch_prefix = launch_prefix_for_root(launchers, root);
        if (launch_prefix.isEmpty())
            continue;

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

            // Keep a per-game identity even when the install path is missing so
            // SearchContext does not collapse every unmounted title onto one filepath.
            QString exe = install_path;
            const QString relative_exe = normalize_install_path(obj.value(QLatin1String("executable")).toString());
            if (!path_missing && !relative_exe.isEmpty()) {
                const QString candidate = install_path + QChar('/') + relative_exe;
                if (QFileInfo::exists(candidate))
                    exe = candidate;
            }

            // Workdir is for starting Dark1HGL itself, not the Windows/Proton
            // game tree — Proton prefixes stay inside Dark1HGL/gogdl.
            entries.emplace_back(
                std::move(id),
                std::move(name),
                std::move(exe),
                build_heroic_launch_cmd(launch_prefix, app_name),
                paths::homePath());
        }
    }
}

void dedupe_entries_by_gog_id(std::vector<GogEntry>& entries)
{
    // One entry per GOG id. Prefer a launch whose install/workdir actually exists.
    HashMap<QString, size_t> id_index;
    std::vector<GogEntry> out;
    out.reserve(entries.size());

    auto entry_usable = [](const GogEntry& entry) -> bool {
        // For Heroic, `exe` is the install path or game binary — not the fallback workdir.
        return QFileInfo::exists(entry.exe);
    };

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
        const bool entry_ok = entry_usable(entry);
        const bool existing_ok = entry_usable(existing);
        const bool entry_heroic = is_heroic_launch(entry.launch_cmd);
        const bool existing_heroic = is_heroic_launch(existing.launch_cmd);

        // Replace missing/broken with a usable sibling.
        if (entry_ok && !existing_ok) {
            existing = std::move(entry);
            continue;
        }
        if (!entry_ok)
            continue;

        // Both usable: keep native Linux start.sh over Heroic/Proton.
        if (!entry_heroic && existing_heroic)
            existing = std::move(entry);
        // else keep existing (native already, or both Heroic)
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
