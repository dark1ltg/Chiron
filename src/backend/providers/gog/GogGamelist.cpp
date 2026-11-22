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

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QTextStream>


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

bool invalid_entry(const GogEntry& entry)
{
    return entry.name.isEmpty()
        || entry.launch_cmd.isEmpty()
        || entry.exe.isEmpty()
        || !QFileInfo::exists(entry.exe);
}

#ifdef Q_OS_LINUX
constexpr QLatin1String HEROIC_FLATPAK_PKG("com.heroicgameslauncher.hgl");

QString find_heroic_launch_prefix(const HashMap<QString, std::vector<QString>>& options)
{
    const auto helper_it = options.find(QLatin1String("launch_helper"));
    if (helper_it != options.cend() && !helper_it->second.empty() && !helper_it->second.front().isEmpty())
        return helper_it->second.front();

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

    const QString flatpak_cfg = paths::homePath()
        % QLatin1String("/.var/app/")
        % HEROIC_FLATPAK_PKG
        % QLatin1String("/config/heroic/");
    if (QFileInfo::exists(flatpak_cfg))
        return QLatin1String("flatpak run ") % HEROIC_FLATPAK_PKG;

    return QStringLiteral("heroic");
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

QString read_goggame_info_name(const QString& install_path, QString* o_id = nullptr)
{
    QDirIterator it(
        install_path,
        QStringList { QStringLiteral("goggame-*.info") },
        QDir::Files,
        QDirIterator::Subdirectories);

    while (it.hasNext()) {
        it.next();
        QFile file(it.filePath());
        if (!file.open(QFile::ReadOnly | QFile::Text))
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;

        const QJsonObject root = doc.object();
        const QString name = root.value(QLatin1String("name")).toString();
        if (o_id && o_id->isEmpty()) {
            const QString game_id = root.value(QLatin1String("gameId")).toString();
            if (!game_id.isEmpty())
                *o_id = game_id;
        }
        if (!name.isEmpty())
            return name;
    }

    return {};
}

void append_heroic_entries(
    std::vector<GogEntry>& entries,
    const HashMap<QString, std::vector<QString>>& options)
{
    const QString launch_prefix = find_heroic_launch_prefix(options);
    const QStringList roots = heroic_config_roots();
    QStringList seen_keys;

    for (const QString& root : roots) {
        const QString installed_path = root + QStringLiteral("/gog_store/installed.json");
        QFile installed_file(installed_path);
        if (!installed_file.open(QFile::ReadOnly | QFile::Text))
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(installed_file.readAll());
        if (!doc.isObject())
            continue;

        const QJsonArray installed = doc.object().value(QLatin1String("installed")).toArray();
        for (const QJsonValue& value : installed) {
            const QJsonObject obj = value.toObject();
            if (obj.isEmpty())
                continue;
            if (obj.value(QLatin1String("is_dlc")).toBool(false))
                continue;

            const QString app_name = obj.value(QLatin1String("appName")).toString();
            const QString install_path = obj.value(QLatin1String("install_path")).toString();
            if (app_name.isEmpty() || install_path.isEmpty() || !QFileInfo::exists(install_path))
                continue;

            const QString dedupe_key = app_name % QChar('|') % install_path;
            if (seen_keys.contains(dedupe_key))
                continue;
            seen_keys.append(dedupe_key);

            QString id = app_name;
            QString name = read_goggame_info_name(install_path, &id);
            if (name.isEmpty())
                name = QFileInfo(install_path).fileName();
            if (name.isEmpty())
                name = app_name;

            QString exe = install_path;
            const QString relative_exe = obj.value(QLatin1String("executable")).toString();
            if (!relative_exe.isEmpty()) {
                const QString candidate = install_path + QChar('/') + relative_exe;
                if (QFileInfo::exists(candidate))
                    exe = candidate;
            }

            // Launch through Heroic/Dark1HGL so Proton/Windows GOG titles work.
            const QString launch_cmd = launch_prefix
                % QLatin1String(" --no-gui heroic://launch/gog/")
                % app_name;

            entries.emplace_back(
                std::move(id),
                std::move(name),
                std::move(exe),
                launch_cmd,
                install_path);
        }
    }
}
#endif // Q_OS_LINUX

std::vector<GogEntry> find_game_entries(const HashMap<QString, std::vector<QString>>& options)
{
    std::vector<GogEntry> entries;

#ifdef Q_OS_WIN
    Q_UNUSED(options);

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

    append_heroic_entries(entries, options);
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
    const std::vector<GogEntry> gogentries = find_game_entries(options);
    Log::info(m_log_tag, LOGMSG("%1 games found").arg(QString::number(gogentries.size())));

    return register_game_entries(gogentries, collection, sctx);
}

} // namespace steam
} // namespace providers
