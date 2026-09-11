#include <retrolarium/smb_level_renderer.hpp>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <iostream>
#include <stdexcept>

namespace {

void requireArraySize(const QJsonArray &array, int expected, const QString &context)
{
    if (array.size() != expected) {
        throw std::runtime_error((context + QStringLiteral(" has the wrong field count"))
            .toStdString());
    }
}

QVector<int> parseHeader(const QJsonArray &raw, const QString &name)
{
    requireArraySize(raw, 6, name + QStringLiteral(" header"));
    QVector<int> result;
    result.reserve(raw.size());
    for (const auto value : raw) result.push_back(value.toInt());
    return result;
}

QVector<retrolarium::SmbAreaObject> parseObjects(
    const QJsonArray &raw, const QString &name)
{
    QVector<retrolarium::SmbAreaObject> result;
    result.reserve(raw.size());
    for (int index = 0; index < raw.size(); ++index) {
        const auto item = raw[index].toArray();
        requireArraySize(item, 4,
            name + QStringLiteral(" object %1").arg(index));
        result.push_back({item[0].toInt(), item[1].toInt(), item[2].toBool(),
            item[3].toInt()});
    }
    return result;
}

QVector<retrolarium::SmbEnemyRecord> parseEnemies(
    const QJsonArray &raw, const QString &name)
{
    QVector<retrolarium::SmbEnemyRecord> result;
    result.reserve(raw.size());
    for (int index = 0; index < raw.size(); ++index) {
        const auto item = raw[index].toArray();
        requireArraySize(item, 8,
            name + QStringLiteral(" enemy record %1").arg(index));
        result.push_back({item[0].toString(), item[1].toInt(), item[2].toInt(),
            item[3].toBool(), item[4].toBool(), item[5].toInt(),
            item[6].toInt(), item[7].toInt()});
    }
    return result;
}

QByteArray sceneDigest(const retrolarium::SmbLevelScene &scene, const QString &name)
{
    if (scene.metatiles.size() != scene.width) {
        throw std::runtime_error((name + QStringLiteral(" column count differs"))
            .toStdString());
    }
    QByteArray payload;
    payload.reserve(scene.width * 13);
    for (int column = 0; column < scene.metatiles.size(); ++column) {
        if (scene.metatiles[column].size() != 13) {
            throw std::runtime_error((name
                + QStringLiteral(" column %1 has the wrong height").arg(column))
                .toStdString());
        }
        for (const int value : scene.metatiles[column]) {
            if (value < 0 || value > 0xff) {
                throw std::runtime_error((name + QStringLiteral(" metatile is out of range"))
                    .toStdString());
            }
            payload.push_back(static_cast<char>(value));
        }
    }
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}

QSet<QString> expectedAreaNames()
{
    QSet<QString> names;
    const struct { const char *prefix; int count; } groups[] {
        {"castle", 6}, {"ground", 22}, {"underground", 3}, {"water", 3},
    };
    for (const auto &group : groups) {
        for (int index = 1; index <= group.count; ++index) {
            names.insert(QString::fromLatin1(group.prefix)
                + QLatin1Char('_') + QString::number(index));
        }
    }
    return names;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    try {
        const auto annRendering = retrolarium::SmbLevelRendering::fromId(
            QStringLiteral("ann"));
        if (annRendering.terrainMetatiles[1] != 0x6a
            || annRendering.residualFlagMetatile != 0x6e
            || !annRendering.upsideDownPipes
            || annRendering.smallObjects.size() != 13
            || annRendering.smallObjects[2][0][0] != 0x5e) {
            throw std::runtime_error("ANN renderer profile differs");
        }
        const auto smb2Rendering = retrolarium::SmbLevelRendering::fromId(
            QStringLiteral("smb2"));
        if (smb2Rendering.terrainMetatiles != QVector<int> {0x6a,0x6b,0x50,0x63}
            || !smb2Rendering.secondaryLedgeSupportMetatiles.isEmpty()
            || smb2Rendering.smallObjects.size() != 16
            || smb2Rendering.smallObjects[1][0][0] != 0xc2
            || smb2Rendering.bridgeMetatile != 0x64) {
            throw std::runtime_error("SMB2 renderer profile differs");
        }
        bool rejectedUnknownRendering = false;
        try {
            static_cast<void>(retrolarium::SmbLevelRendering::fromId(
                QStringLiteral("unknown")));
        } catch (const retrolarium::NesResourceDocumentError &) {
            rejectedUnknownRendering = true;
        }
        if (!rejectedUnknownRendering) {
            throw std::runtime_error("Unknown SMB renderer profile was accepted");
        }
        const auto unsupportedLedge = retrolarium::renderSmbLevelScene(
            QStringLiteral("ground_1"), QVector<int> {0,0,0,1,0,0},
            QVector<retrolarium::SmbAreaObject> {{0,4,false,0x12}}, {}, 0,
            smb2Rendering);
        if (unsupportedLedge.metatiles[0][4] != 0x8a
            || unsupportedLedge.metatiles[1][4] != 0x8b
            || unsupportedLedge.metatiles[2][4] != 0x8c
            || unsupportedLedge.metatiles[1][5] != 0) {
            throw std::runtime_error("SMB2 supportless ledge rendering differs");
        }

        QFile source(QStringLiteral(":/fixtures/smb1_ju_level_scenes.json"));
        if (!source.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("cannot open embedded SMB JU golden corpus");
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(source.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            throw std::runtime_error("SMB JU golden corpus is not valid JSON");
        }
        const auto root = document.object();
        if (root[QStringLiteral("schema")].toString()
                != QStringLiteral("retrolarium.smb-level-scene-golden")
            || root[QStringLiteral("schema_version")].toInt() != 1
            || root[QStringLiteral("profile")].toString() != QStringLiteral("smb1.ju")) {
            throw std::runtime_error("SMB JU golden corpus identity differs");
        }

        const auto areas = root[QStringLiteral("areas")].toArray();
        if (areas.size() != 34) {
            throw std::runtime_error("SMB JU golden corpus does not contain 34 areas");
        }
        auto missingNames = expectedAreaNames();
        for (const auto rawArea : areas) {
            const auto area = rawArea.toObject();
            const QString name = area[QStringLiteral("name")].toString();
            if (!missingNames.remove(name)) {
                throw std::runtime_error((name + QStringLiteral(" is duplicated or unknown"))
                    .toStdString());
            }
            const auto scene = retrolarium::renderSmbLevelScene(name,
                parseHeader(area[QStringLiteral("header")].toArray(), name),
                parseObjects(area[QStringLiteral("objects")].toArray(), name),
                parseEnemies(area[QStringLiteral("enemies")].toArray(), name),
                area[QStringLiteral("world")].toInt());
            const auto expected = area[QStringLiteral("expected")].toObject();
            if (scene.width != expected[QStringLiteral("width")].toInt()) {
                throw std::runtime_error((name + QStringLiteral(" width differs"))
                    .toStdString());
            }
            if (sceneDigest(scene, name)
                != expected[QStringLiteral("metatile_sha256")].toString().toLatin1()) {
                throw std::runtime_error((name + QStringLiteral(" metatile grid differs"))
                    .toStdString());
            }
            const auto expectedEnemies = expected[QStringLiteral("enemies")].toArray();
            if (scene.enemies.size() != expectedEnemies.size()) {
                throw std::runtime_error((name + QStringLiteral(" enemy count differs"))
                    .toStdString());
            }
            for (int index = 0; index < scene.enemies.size(); ++index) {
                const auto placement = expectedEnemies[index].toArray();
                requireArraySize(placement, 3,
                    name + QStringLiteral(" expected enemy %1").arg(index));
                const auto &enemy = scene.enemies[index];
                if (enemy.index != placement[0].toInt()
                    || enemy.page != placement[1].toInt()
                    || enemy.x != placement[2].toInt()) {
                    throw std::runtime_error((name
                        + QStringLiteral(" enemy placement %1 differs").arg(index))
                        .toStdString());
                }
            }
            const auto expectedObjects = expected[QStringLiteral("objects")].toArray();
            if (scene.objects.size() != expectedObjects.size()) {
                throw std::runtime_error((name + QStringLiteral(" object count differs"))
                    .toStdString());
            }
            for (int index = 0; index < scene.objects.size(); ++index) {
                const auto placement = expectedObjects[index].toArray();
                requireArraySize(placement, 4,
                    name + QStringLiteral(" expected object %1").arg(index));
                const auto &object = scene.objects[index];
                if (object.index != placement[0].toInt()
                    || object.page != placement[1].toInt()
                    || object.x != placement[2].toInt()
                    || object.width != placement[3].toInt()) {
                    throw std::runtime_error((name
                        + QStringLiteral(" object placement %1 differs").arg(index))
                        .toStdString());
                }
            }
        }
        if (!missingNames.isEmpty()) {
            throw std::runtime_error("SMB JU golden corpus is missing named areas");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
