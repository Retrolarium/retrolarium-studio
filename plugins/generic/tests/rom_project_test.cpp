// A ROM opened as it is (profile rom.v1) on synthetic images: the views a NES
// and a Mega Drive ROM get, a byte typed in the hex table and undone, a tile
// read and painted back in the cartridge's own bit layout, the header, the
// Mega Drive checksum fixed, and Save writing the working copy only.
// Needs no project.
//   rom_project_test

#include "rom/rom_models.hpp"
#include "rom/rom_session.hpp"
#include "sdk/project_file.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUndoStack>

#include <iostream>
#include <stdexcept>

using namespace rl;

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "a file is written");
}

std::unique_ptr<rom::RomSession> project(const QTemporaryDir& directory, const QString& platform,
                                         const QByteArray& image) {
    QDir(directory.path()).mkpath(QStringLiteral("original"));
    writeFile(directory.filePath(QStringLiteral("game.bin")), image);
    writeFile(directory.filePath(QStringLiteral("original/game.bin")), image);
    const QString text = QStringLiteral(
                             "schema = \"retrolarium.project\"\nschema_version = 1\n\n[project]\nid = \"game\"\n"
                             "name = \"Game\"\nplatform = \"%1\"\nprofile = \"rom.v1\"\nsource_mode = \"rom\"\n\n"
                             "[source]\nrom = \"game.bin\"\noriginal = \"original/game.bin\"\nsha256 = \"x\"\n")
                             .arg(platform);
    const QString path = directory.filePath(QStringLiteral("game.rlj"));
    writeFile(path, text.toUtf8());
    QString error;
    const sdk::ProjectFile file = sdk::ProjectFile::load(path, &error);
    expect(file.isValid(), "the project file reads");
    auto session = rom::RomSession::open(file, &error);
    expect(session != nullptr, "the ROM project opens");
    return session;
}

QStringList kindIds(const rom::RomSession& session) {
    QStringList ids;
    for (const sdk::Kind& kind : session.kinds()) ids << kind.id;
    return ids;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        // A NES image: iNES header, 16 KB PRG, 8 KB CHR.
        QByteArray nes(16 + 16384 + 8192, '\0');
        nes.replace(0, 4, QByteArray("NES\x1a", 4));
        nes[4] = 1;
        nes[5] = 1;
        nes[6] = 0x01;   // vertical mirroring, mapper 0
        const int chr = 16 + 16384;
        // Tile 0: row 0 all pen 1, row 1 all pen 2, row 2 pen 3 at the left.
        nes[chr + 0] = char(0xff);
        nes[chr + 9] = char(0xff);
        nes[chr + 2] = char(0x80);
        nes[chr + 10] = char(0x80);

        QTemporaryDir nesDir;
        auto session = project(nesDir, QStringLiteral("nes"), nes);
        expect(kindIds(*session) == QStringList({"hex", "tiles", "header", "playtest"}), "a NES ROM's views");
        expect(session->graphicsArea() == qMakePair(chr, 8192), "the CHR ROM holds the tiles");

        rom::HexModel hex(session.get());
        expect(hex.rowCount() == nes.size() / 16, "sixteen bytes a row");
        expect(hex.cell(0, 1) == QLatin1String("4E") && hex.cell(0, 17).startsWith(QLatin1String("NES")),
               "bytes and their text");
        QString error;
        expect(!hex.setCell(1, 1, QStringLiteral("1G0"), &error), "not a byte is refused");
        expect(hex.setCell(1, 1, QStringLiteral("a5"), &error), "a byte typed");
        expect(quint8(session->rom().at(16)) == 0xa5 && hex.isRowDirty(1) && !hex.isRowDirty(0), "the row changed");
        session->undoStack()->undo();
        expect(session->rom().at(16) == 0 && !hex.isRowDirty(1), "undone");
        expect(hex.setField(QStringLiteral("goto"), QStringLiteral("$4010"), &error) && hex.currentRow() == 0x401,
               "go to an offset");

        rom::RomTileModel tiles(session.get());
        expect(tiles.penCount() == 4 && tiles.tileCount() == 256, "a bank of NES tiles");
        const QVector<quint8> pixels = tiles.tilePixels(0);
        expect(pixels[0] == 1 && pixels[7] == 1 && pixels[8] == 2 && pixels[16] == 3 && pixels[17] == 0,
               "planes decoded");
        tiles.beginStroke();
        expect(tiles.setPixel(QPoint(7, 2), 3), "a pixel painted");
        tiles.endStroke();
        expect(quint8(session->rom().at(chr + 2)) == 0x81 && quint8(session->rom().at(chr + 10)) == 0x81,
               "painted into both planes");
        expect(tiles.isTileDirty(0) && !tiles.isTileDirty(1), "the tile changed");

        rom::HeaderModel header(session.get());
        bool mapper = false;
        for (int row = 0; row < header.rowCount(); ++row) {
            if (header.cell(row, 0) == QLatin1String("Mapper")) mapper = header.cell(row, 1) == QLatin1String("0");
        }
        expect(mapper, "the mapper read");

        expect(session->save(&error), "saved");
        QFile working(nesDir.filePath(QStringLiteral("game.bin")));
        QFile original(nesDir.filePath(QStringLiteral("original/game.bin")));
        expect(working.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly), "both files open");
        expect(working.readAll() == session->rom() && original.readAll() == nes, "the working copy saved, the original kept");

        // A Mega Drive image with a wrong checksum.
        QByteArray md(0x400, '\0');
        md.replace(0x100, 16, QByteArray("SEGA MEGA DRIVE "));
        md[0x200] = 0x12;
        md[0x201] = 0x34;
        md[0x3fe] = 0x00;
        md[0x3ff] = 0x01;
        QTemporaryDir mdDir;
        auto drive = project(mdDir, QStringLiteral("megadrive"), md);
        rom::HeaderModel mdHeader(drive.get());
        expect(mdHeader.actions().size() == 1, "a wrong checksum can be fixed");
        mdHeader.triggerAction(QStringLiteral("fix-checksum"));
        expect(quint8(drive->rom().at(0x18e)) == 0x12 && quint8(drive->rom().at(0x18f)) == 0x35, "the checksum written");
        expect(mdHeader.actions().isEmpty(), "and then right");
        rom::RomTileModel mdTiles(drive.get());
        expect(mdTiles.penCount() == 16 && mdTiles.tilePixels(16)[0] == 1 && mdTiles.tilePixels(16)[1] == 2,
               "Mega Drive nibbles");

        QTemporaryDir otherDir;
        auto other = project(otherDir, QStringLiteral("unknown"), QByteArray(64, '\x55'));
        expect(kindIds(*other) == QStringList({"hex", "header"}), "an unknown system: bytes and header only");
    } catch (const std::exception& problem) {
        std::cerr << "FAIL: " << problem.what() << "\n";
        return 1;
    }
    std::cout << "rom_project_test: ok\n";
    return 0;
}
