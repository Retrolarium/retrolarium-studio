#include "sdk/game_names.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "sdk/project_file.hpp"

namespace rl::sdk {

namespace {

GameNames& installed() {
    static GameNames names;
    return names;
}

// 6, 0x06 and $06 all name id 6.
bool numericKey(const QString& key, int* value) {
    bool ok = false;
    if (key.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        *value = key.mid(2).toInt(&ok, 16);
    } else if (key.startsWith(QLatin1Char('$'))) {
        *value = key.mid(1).toInt(&ok, 16);
    } else {
        *value = key.toInt(&ok, 10);
    }
    return ok;
}

}  // namespace

GameNames GameNames::fromRepository(const QString& repository, QString* error) {
    GameNames result;
    if (repository.isEmpty()) {
        return result;
    }
    QFile file(QDir(repository).filePath(QStringLiteral("config/names.json")));
    if (!file.exists()) {
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError ||
        root.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.names")) {
        if (error) *error = QStringLiteral("%1 is not a names document").arg(file.fileName());
        return result;
    }
    result.catalogs_ = root.value(QStringLiteral("catalogs")).toObject();
    const QJsonObject categories = root.value(QStringLiteral("names")).toObject();
    for (auto category = categories.begin(); category != categories.end(); ++category) {
        const QJsonObject entries = category.value().toObject();
        QHash<QString, QString>& names = result.names_[category.key()];
        QHash<int, QString>& numbered = result.numbered_[category.key()];
        for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
            const QString name = entry.value().toString();
            names.insert(entry.key(), name);
            int id = 0;
            if (numericKey(entry.key(), &id)) {
                numbered.insert(id, name);
            }
        }
    }
    return result;
}

GameNames GameNames::forProject(const ProjectFile& project, QString* error) {
    const QString repository = project.string(QStringLiteral("source.repository"));
    if (!repository.isEmpty()) return fromRepository(project.resolve(repository), error);
    // A project the kit generates in a game repository sits in
    // <repository>/.retrolarium/<version>/, naming its repository or not
    // (Flicky's names its source snapshot instead).
    QDir up = QFileInfo(project.path()).absoluteDir();
    if (up.cdUp() && up.dirName() == QLatin1String(".retrolarium") && up.cdUp() &&
        QFileInfo::exists(up.filePath(QStringLiteral("config/names.json")))) {
        return fromRepository(up.absolutePath(), error);
    }
    return GameNames();
}

const GameNames& GameNames::current() {
    return installed();
}

void GameNames::install(const GameNames& names) {
    installed() = names;
}

QString GameNames::name(const QString& category, const QString& key) const {
    return names_.value(category).value(key);
}

QString GameNames::name(const QString& category, int id) const {
    return numbered_.value(category).value(id);
}

QMap<int, QString> GameNames::numbered(const QString& category) const {
    QMap<int, QString> result;
    const QHash<int, QString> names = numbered_.value(category);
    for (auto it = names.begin(); it != names.end(); ++it) result.insert(it.key(), it.value());
    return result;
}

QMap<int, QString> gameNames(const char* category) {
    return GameNames::current().numbered(QString::fromLatin1(category));
}

QString gameName(const char* category, int id, const QString& fallback) {
    const QString name = GameNames::current().name(QString::fromLatin1(category), id);
    return name.isEmpty() ? fallback : name;
}

QJsonValue gameCatalog(const char* key) {
    return GameNames::current().catalog(QString::fromLatin1(key));
}

QString gameName(const char* category, const QString& key, const QString& fallback) {
    const QString name = GameNames::current().name(QString::fromLatin1(category), key);
    return name.isEmpty() ? fallback : name;
}

}  // namespace rl::sdk
