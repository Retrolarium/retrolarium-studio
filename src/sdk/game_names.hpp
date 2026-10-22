#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>

namespace rl::sdk {

class ProjectFile;

// The names inside a game (its characters, enemies, items, places, levels)
// are the game's, not the studio's: the game's repository keeps them in
// config/names.json, in English, and the studio shows them as they are,
// untranslated. The studio keeps none of them; where a name is missing (no
// repository, no file, no entry) a neutral label such as "Enemy $06" stands
// in.
//
//     {"schema": "retrolarium.names", "schema_version": 1,
//      "names": {"enemies": {"0x06": "...", "0x07": "..."},
//                "players": {"0": "...", "1": "..."}}}
//
// A category maps keys to names; a numeric key may be written 6, 0x06 or $06.
// A game whose lists carry more than names (an animation's frames, the order
// of a sound table) keeps them under "catalogs", read with gameCatalog().
// The shell loads the names of the project it opens, before the plugin's
// session, so a plugin's catalogs read them through gameName().
class GameNames {
public:
    // The names of a game repository (config/names.json under it); empty when
    // the file is missing or unreadable.
    static GameNames fromRepository(const QString& repository, QString* error = nullptr);
    // The names of the repository a project file names (source.repository).
    static GameNames forProject(const ProjectFile& project, QString* error = nullptr);

    // The names the open project uses; install() replaces them.
    static const GameNames& current();
    static void install(const GameNames& names);

    bool isEmpty() const { return names_.isEmpty(); }
    // A name, or an empty string when the category has no such key.
    QString name(const QString& category, const QString& key) const;
    QString name(const QString& category, int id) const;
    // A category's numbered names in order of their ids.
    QMap<int, QString> numbered(const QString& category) const;
    // A catalog, or undefined when the game has none by that key.
    QJsonValue catalog(const QString& key) const { return catalogs_.value(key); }

private:
    QHash<QString, QHash<QString, QString>> names_;  // category -> key -> name
    QHash<QString, QHash<int, QString>> numbered_;   // category -> id -> name
    QJsonObject catalogs_;
};

// The open project's name for `id` in `category`, else `fallback`.
QString gameName(const char* category, int id, const QString& fallback);
QString gameName(const char* category, const QString& key, const QString& fallback);
// The open project's catalog `key`; undefined when it has none.
QJsonValue gameCatalog(const char* key);
// The open project's numbered names in `category`, by id.
QMap<int, QString> gameNames(const char* category);

}  // namespace rl::sdk
