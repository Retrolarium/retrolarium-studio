// Hand-edited documents with records out of range or arrays cut short must
// not reach memory they do not own. The game libraries are linked as checked
// copies (Qt's and libstdc++'s index checks on, see plugins/CMakeLists.txt):
// a read or write past an array aborts this test instead of passing unseen.

#include "castle_excellent/data/levels.hpp"
#include "castle_excellent/plugin/screen_model.hpp"
#include "smb/render/level_renderer.hpp"
#include "solomon/data/documents.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

QString writeJson(const QTemporaryDir& dir, const QString& name, const QJsonObject& root) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0) {
        throw std::runtime_error("cannot write a fixture");
    }
    return path;
}

QJsonObject castleRooms(int width, int height, const QJsonArray& cells) {
    return QJsonObject{{QStringLiteral("schema"), QStringLiteral("retrolarium.castle-excellent-levels")},
                       {QStringLiteral("width"), width},
                       {QStringLiteral("height"), height},
                       {QStringLiteral("rooms"), QJsonArray{QJsonObject{{QStringLiteral("cells"), cells}}}}};
}

// Castle rooms: a size the screen cannot hold is refused when the document
// opens; rows cut short grow to the cell an edit writes.
void castleRoomsHoldUp(const QTemporaryDir& dir) {
    using rl::castle_excellent::CastleExcellentError;
    using rl::castle_excellent::CastleExcellentLevels;
    for (const auto& size :
         {QPair<int, int>{128, 20}, QPair<int, int>{20, 64}, QPair<int, int>{0, 20}, QPair<int, int>{-3, 20}}) {
        bool refused = false;
        try {
            static_cast<void>(CastleExcellentLevels::load(
                writeJson(dir, QStringLiteral("big.json"), castleRooms(size.first, size.second, {}))));
        } catch (const CastleExcellentError&) {
            refused = true;
        }
        expect(refused, "a Castle room of an impossible size is refused");
    }
    CastleExcellentLevels levels = CastleExcellentLevels::load(
        writeJson(dir, QStringLiteral("short.json"), castleRooms(20, 20, QJsonArray{QJsonArray{0, 0}})));
    expect(levels.cells(0).size() == 400, "short rows read as a whole room");
    levels.setCell(0, QPoint(19, 19), 0);
    const QString saved = dir.filePath(QStringLiteral("saved.json"));
    levels.save(saved);
    QFile written(saved);
    if (!written.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read the saved rooms");
    const QJsonArray rows = QJsonDocument::fromJson(written.readAll())
                                .object()
                                .value(QStringLiteral("rooms"))
                                .toArray()
                                .at(0)
                                .toObject()
                                .value(QStringLiteral("cells"))
                                .toArray();
    expect(rows.size() == 20 && rows.at(19).toArray().size() == 20, "an edit grows short rows to its cell");
}

// Castle screens: a palette written past a short attribute list grows it.
void castleScreensHoldUp() {
    using rl::castle_excellent::screens::paletteOf;
    using rl::castle_excellent::screens::withPalette;
    const QJsonArray attributes = withPalette(QJsonArray{}, 30 * 32 - 1, 3);
    expect(paletteOf(attributes, 30 * 32 - 1) == 3, "a palette set past a short attribute list is kept");
}

// Solomon rooms: block planes cut short grow to the block an edit writes.
void solomonRoomsHoldUp() {
    rl::solomon::SolomonRooms rooms(QJsonObject{
        {QStringLiteral("rooms"), QJsonArray{QJsonObject{{QStringLiteral("brown"), QJsonArray{}},
                                                         {QStringLiteral("white"), QJsonArray{QJsonArray{0}}}}}}});
    expect(!rooms.block(0, QPoint(15, 11), false), "a missing block reads as none");
    rooms.setBlock(0, QPoint(15, 11), false, true);
    expect(rooms.block(0, QPoint(15, 11), false), "a block set past a short plane is kept");
    rooms.setBlock(0, QPoint(7, 0), true, true);
    expect(rooms.block(0, QPoint(7, 0), true), "a block set past a short row is kept");
}

// SMB: a modifier object before the area's first column, or past it, has
// no column to modify; the area still renders.
void smbAreasHoldUp(const QString& corpus) {
    QFile source(corpus);
    if (!source.open(QIODevice::ReadOnly)) {
        std::cout << "SMB part skipped: no level corpus\n";
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(source.readAll()).object();
    const auto rendering = rl::smb::SmbLevelRendering::fromResource(
        root.value(QStringLiteral("level_tables")).toObject(), root.value(QStringLiteral("rendering")).toString());
    const QVector<int> header{0, 0, 0, 1, 0, 0};
    const QVector<rl::smb::SmbAreaObject> plain{{0, 4, false, 0x12}};
    QVector<rl::smb::SmbAreaObject> malformed{{-1, 14, false, 0x40}, {-100, 14, false, 0x41}};
    malformed += plain;
    const auto expected = rl::smb::renderSmbLevelScene(QStringLiteral("ground_1"), header, plain, {}, 0, rendering);
    const auto scene = rl::smb::renderSmbLevelScene(QStringLiteral("ground_1"), header, malformed, {}, 0, rendering);
    expect(scene.metatiles == expected.metatiles, "modifiers outside the area leave the area as it was");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        castleRoomsHoldUp(dir);
        castleScreensHoldUp();
        solomonRoomsHoldUp();
        smbAreasHoldUp(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) return 1;
    std::cout << "malformed documents: rooms, screens and areas hold up\n";
    return 0;
}
