// Removing and re-inserting every record of every SMB1 JU area must leave all
// other records at their page and column, and give back the same stream
// positions. Reads the areas from the level renderer's golden corpus.
//   smb_area_streams_test <game sources>/smb1_src/tests/retrolarium/smb1_ju_level_scenes.json

#include "smb/data/area_streams.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace rl::smb;

namespace {

using Position = std::tuple<int, int, int, int, QString>;  // page, column, row, payload, kind

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QVector<Position> objectPositions(const QVector<SmbAreaObject>& objects) {
    const QVector<int> pages = objectPages(objects);
    QVector<Position> result;
    for (int i = 0; i < objects.size(); ++i) {
        if (!isPageCommand(objects.at(i))) {
            result << Position(pages.at(i), objects.at(i).column, objects.at(i).row, objects.at(i).control, QString());
        }
    }
    return result;
}

QVector<Position> enemyPositions(const QVector<SmbEnemyRecord>& enemies) {
    const QVector<int> pages = enemyPages(enemies);
    QVector<Position> result;
    for (int i = 0; i < enemies.size(); ++i) {
        const SmbEnemyRecord& e = enemies.at(i);
        if (e.kind != QLatin1String("page")) {
            result << Position(pages.at(i), e.column, e.row, e.objectOrPage * 2 + (e.hardMode ? 1 : 0), e.kind);
        }
    }
    return result;
}

// Same positions, ignoring the order of records that share a page and column.
QVector<Position> sorted(QVector<Position> positions) {
    std::stable_sort(positions.begin(), positions.end());
    return positions;
}

int objectBytes(const QVector<SmbAreaObject>& objects) {
    return 2 * int(objects.size());
}

int enemyBytes(const QVector<SmbEnemyRecord>& enemies) {
    int bytes = 0;
    for (const SmbEnemyRecord& e : enemies) bytes += e.kind == QLatin1String("entrance") ? 3 : 2;
    return bytes;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        if (argc < 2) fail(QStringLiteral("usage: smb_area_streams_test <smb1_ju_level_scenes.json>"));
        QFile source(QString::fromLocal8Bit(argv[1]));
        if (!source.open(QIODevice::ReadOnly)) fail(QStringLiteral("cannot open the corpus"));
        const QJsonArray areas = QJsonDocument::fromJson(source.readAll()).object().value(QStringLiteral("areas")).toArray();
        if (areas.size() != 34) fail(QStringLiteral("the corpus does not hold 34 areas"));
        int checks = 0;
        int unsortedAreas = 0;
        int maxGrowth = 0;
        for (const QJsonValue& value : areas) {
            const QJsonObject area = value.toObject();
            const QString name = area.value(QStringLiteral("name")).toString();
            QVector<SmbAreaObject> objects;
            for (const QJsonValue& item : area.value(QStringLiteral("objects")).toArray()) {
                const QJsonArray o = item.toArray();
                objects.push_back({o.at(0).toInt(), o.at(1).toInt(), o.at(2).toBool(), o.at(3).toInt()});
            }
            QVector<SmbEnemyRecord> enemies;
            for (const QJsonValue& item : area.value(QStringLiteral("enemies")).toArray()) {
                const QJsonArray e = item.toArray();
                enemies.push_back({e.at(0).toString(), e.at(1).toInt(), e.at(2).toInt(), e.at(3).toBool(),
                                   e.at(4).toBool(), e.at(5).toInt(), e.at(6).toInt(), e.at(7).toInt()});
            }
            const QVector<Position> objectsBefore = objectPositions(objects);
            if (sorted(objectsBefore) != objectsBefore) ++unsortedAreas;
            for (int i = 0; i < objects.size(); ++i) {
                if (isPageCommand(objects.at(i))) continue;
                const int page = objectPages(objects).at(i);
                QVector<SmbAreaObject> edited = objects;
                removeObject(edited, i);
                QVector<Position> expected = objectsBefore;
                expected.removeOne(Position(page, objects.at(i).column, objects.at(i).row, objects.at(i).control, QString()));
                if (objectPositions(edited) != expected) {
                    fail(QStringLiteral("%1: removing object %2 moved other objects").arg(name).arg(i));
                }
                insertObject(edited, page, objects.at(i));
                if (sorted(objectPositions(edited)) != sorted(objectsBefore)) {
                    fail(QStringLiteral("%1: re-inserting object %2 changed positions").arg(name).arg(i));
                }
                maxGrowth = std::max(maxGrowth, objectBytes(edited) - objectBytes(objects));
                ++checks;
            }
            const QVector<Position> enemiesBefore = enemyPositions(enemies);
            for (int i = 0; i < enemies.size(); ++i) {
                if (enemies.at(i).kind == QLatin1String("page")) continue;
                const int page = enemyPages(enemies).at(i);
                const SmbEnemyRecord& e = enemies.at(i);
                QVector<SmbEnemyRecord> edited = enemies;
                removeEnemy(edited, i);
                QVector<Position> expected = enemiesBefore;
                expected.removeOne(Position(page, e.column, e.row, e.objectOrPage * 2 + (e.hardMode ? 1 : 0), e.kind));
                if (enemyPositions(edited) != expected) {
                    fail(QStringLiteral("%1: removing enemy record %2 moved other records").arg(name).arg(i));
                }
                insertEnemy(edited, page, e);
                if (sorted(enemyPositions(edited)) != sorted(enemiesBefore)) {
                    fail(QStringLiteral("%1: re-inserting enemy record %2 changed positions").arg(name).arg(i));
                }
                maxGrowth = std::max(maxGrowth, enemyBytes(edited) - enemyBytes(enemies));
                ++checks;
            }
        }
        std::cout << checks << " remove/insert checks passed; " << unsortedAreas
                  << " areas store objects out of column order; largest growth " << maxGrowth << " bytes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
