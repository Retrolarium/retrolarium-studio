// A ROM opened as it is (profile rom.v1) on synthetic images: the views a NES
// and a Mega Drive ROM get, a byte typed in the hex editor and undone, a
// selection filled, nothing written past the end, a tile
// read and painted back in the cartridge's own bit layout, another layout
// keeping the place, the header, the Mega Drive checksum fixed, and Save
// writing the working copy only. Every tile layout round-trips, and pictures
// among random bytes are found, their edges and layout too, even where they
// start between tiles. Needs no project.
//   rom_project_test

#include "rom/rom_models.hpp"
#include "rom/rom_session.hpp"
#include "rom/tile_formats.hpp"
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

// Random bytes, then 128 tiles of a picture in `id`, then random bytes.
QByteArray picturesAmidNoise(const QString& id) {
    const rom::tiles::Format& format = rom::tiles::format(id);
    quint32 seed = 12345;
    auto noise = [&seed](int length) {
        QByteArray bytes(length, '\0');
        for (char& byte : bytes) {
            seed = seed * 1103515245u + 12345u;
            byte = char((seed >> 16) & 0xff);
        }
        return bytes;
    };
    QByteArray bytes = noise(8192);
    const int colors = 1 << format.bits;
    for (int tile = 0; tile < 128; ++tile) {
        rom::tiles::Pixels pixels{};
        for (int i = 0; i < 64; ++i) pixels[i] = quint8(((i % 8 + 1) / 3 + (i / 16) * 5 + tile * 7) % colors);
        QByteArray packed(format.bytes, '\0');
        rom::tiles::encode(format, pixels, packed.data());
        bytes += packed;
    }
    return bytes + noise(8192);
}

void tileFormats() {
    for (const rom::tiles::Format& format : rom::tiles::formats()) {
        rom::tiles::Pixels pixels{};
        for (int i = 0; i < 64; ++i) pixels[i] = quint8((i * 7 + i / 8) % (1 << format.bits));
        QByteArray packed(format.bytes, '\0');
        rom::tiles::encode(format, pixels, packed.data());
        expect(rom::tiles::decode(format, packed.constData()) == pixels, "a layout round-trips");
    }
    for (const char* id : {"nes", "snes4", "md", "8bpp"}) {
        const QByteArray bytes = picturesAmidNoise(QLatin1String(id));
        expect(rom::tiles::detect(bytes, 0) == QLatin1String(id), "the layout of pictures among noise guessed");
        const rom::tiles::Format& format = rom::tiles::format(QLatin1String(id));
        const QVector<rom::tiles::Section> found = rom::tiles::sections(bytes, 0, format);
        expect(found.size() == 1 && found.at(0).offset == 8192 && found.at(0).tiles == 128,
               "the pictures found as one section");
    }
    expect(rom::tiles::detect(QByteArray(4096, '\0'), 0).isEmpty(), "nothing guessed for empty bytes");

    const QVector<rom::tiles::Group> nes = rom::tiles::groups(picturesAmidNoise(QStringLiteral("nes")), 0);
    expect(nes.size() == 1 && nes.at(0).offset == 8192 && nes.at(0).tiles == 128 &&
               nes.at(0).format == QLatin1String("nes"),
           "a group of tiles: its edges and its layout");
    const QByteArray shifted = QByteArray(6, char(0x5a)) + picturesAmidNoise(QStringLiteral("md"));
    const QVector<rom::tiles::Group> md = rom::tiles::groups(shifted, 0, {QStringLiteral("md")});
    expect(md.size() == 1 && md.at(0).offset == 8198 && md.at(0).tiles == 128, "a group that starts between tiles");
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
        expect(hex.size() == nes.size() && hex.bytes(0, 4) == QByteArray("NES\x1a", 4), "the bytes");
        QString error;
        expect(!hex.writeBytes(nes.size() - 1, QByteArray(2, char(1)), QStringLiteral("Past"), &error),
               "nothing written past the end");
        expect(hex.writeBytes(16, QByteArray(1, char(0xa5)), QStringLiteral("Type"), &error), "a byte typed");
        expect(quint8(session->rom().at(16)) == 0xa5 && hex.isByteDirty(16) && !hex.isByteDirty(15), "the byte changed");
        session->undoStack()->undo();
        expect(session->rom().at(16) == 0 && !hex.isByteDirty(16), "undone");
        expect(hex.setField(QStringLiteral("goto"), QStringLiteral("$4010"), &error) && hex.cursor() == 0x4010,
               "go to an offset");
        hex.setSelection(0x2f, 0x20);
        expect(hex.hasSelection() && hex.selectionFirst() == 0x20 && hex.selectionLast() == 0x2f, "a selection");
        expect(hex.writeBytes(0x20, QByteArray(16, char(0xff)), QStringLiteral("Fill"), &error) &&
                   session->rom().mid(0x20, 16) == QByteArray(16, char(0xff)),
               "the selection filled");
        session->undoStack()->undo();
        expect(session->rom().mid(0x20, 16) == nes.mid(0x20, 16), "and the fill undone");

        rom::RomTileModel whole(session.get());
        expect(whole.penCount() == 4 && whole.tileCount() == (16384 + 8192) / 16, "every NES tile of the file");
        const QVector<sdk::DocumentGroup> tileDocuments = session->documents(QStringLiteral("tiles"));
        expect(tileDocuments.size() == 1 && tileDocuments.at(0).documents.size() == 2 &&
                   tileDocuments.at(0).documents.at(1).id == QLatin1String("group-0"),
               "the whole ROM, then the CHR ROM");
        rom::RomTileModel tiles(session.get(), QStringLiteral("group-0"));
        expect(tiles.tileCount() == 512, "the CHR ROM's tiles");
        const QVector<quint8> pixels = tiles.tilePixels(0);
        expect(pixels[0] == 1 && pixels[7] == 1 && pixels[8] == 2 && pixels[16] == 3 && pixels[17] == 0,
               "planes decoded");
        tiles.beginStroke();
        expect(tiles.setPixel(QPoint(7, 2), 3), "a pixel painted");
        tiles.endStroke();
        expect(quint8(session->rom().at(chr + 2)) == 0x81 && quint8(session->rom().at(chr + 10)) == 0x81,
               "painted into both planes");
        expect(tiles.isTileDirty(0) && !tiles.isTileDirty(1), "the tile changed");
        tiles.setCurrentTile(5);
        tiles.setFormat(QStringLiteral("md"));
        expect(tiles.penCount() == 16 && tiles.currentTile() == 2, "another layout keeps the place");
        tiles.setFormat(QStringLiteral("nes"));
        const rom::tiles::Pixels moved =
            rom::tiles::decode(rom::tiles::format(QStringLiteral("nes")), session->rom().constData() + chr + 1);
        expect(tiles.canShiftStart() && tiles.shiftStart(1) &&
                   tiles.tilePixels(0) == QVector<quint8>(moved.begin(), moved.end()),
               "the start moved a byte");
        expect(tiles.shiftStart(-1) && tiles.tilePixels(0)[0] == 1, "and back");

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
        expect(kindIds(*other) == QStringList({"hex", "tiles", "header"}), "an unknown system: no playtest");
        rom::RomTileModel otherTiles(other.get());
        expect(otherTiles.formatId() == QLatin1String("md") && otherTiles.penCount() == 16,
               "no layout guessed: 4 bits a pixel");
        tileFormats();
    } catch (const std::exception& problem) {
        std::cerr << "FAIL: " << problem.what() << "\n";
        return 1;
    }
    std::cout << "rom_project_test: ok\n";
    return 0;
}
