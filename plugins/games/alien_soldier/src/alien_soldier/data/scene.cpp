#include "alien_soldier/data/scene.hpp"

#include "megadrive/vdp.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

constexpr int kVramSize = 0x10000;
constexpr int kRamSize = 0x10000;
// src/ram_addrs.inc: the first thing above the coarse map, where its region ends.
constexpr quint32 kTerrainCollisionBuffer = 0xFFFF7800u;

int signedWord(int value) { return value >= 0x8000 ? value - 0x10000 : value; }

QJsonObject findByLabel(const QJsonArray& items, const QString& label) {
    const int index = indexOfLabel(items, label);
    return index < 0 ? QJsonObject() : items.at(index).toObject();
}

// The values of a map payload as the bytes the game decompresses.
QByteArray mapBytes(const QJsonObject& payload) {
    const bool words = payload.value(QStringLiteral("role")).toString() == QLatin1String("patterns");
    QByteArray bytes;
    for (const QJsonValue& value : payload.value(QStringLiteral("values")).toArray()) {
        const int number = value.toInt();
        if (words) bytes.append(char(number >> 8));
        bytes.append(char(number & 0xFF));
    }
    return bytes;
}

// What a RAM write puts there: the edited values when a document holds the
// payload, else the bytes the kit gave with the scene.
QByteArray writeBytes(const Documents& documents, const QJsonObject& write) {
    const QString source = write.value(QStringLiteral("source")).toString();
    for (Doc doc : {Doc::Maps, Doc::Screens}) {
        const QJsonObject payload = findByLabel(documents[doc].value(QStringLiteral("payloads")).toArray(), source);
        if (!payload.isEmpty()) return mapBytes(payload);
    }
    return hexBytes(write.value(QStringLiteral("data")).toString());
}

void applyWrites(Scene& scene, const Documents& documents, const QJsonArray& writes) {
    for (const QJsonValue& value : writes) {
        const QJsonObject write = value.toObject();
        const QByteArray bytes = writeBytes(documents, write);
        const int offset = int(quint32(write.value(QStringLiteral("ram")).toDouble()) - kRamBase);
        if (offset < 0 || offset >= kRamSize) continue;
        const int length = std::min<int>(bytes.size(), kRamSize - offset);
        std::copy(bytes.cbegin(), bytes.cbegin() + length, scene.ram.begin() + offset);
        scene.writes.append({write.value(QStringLiteral("source")).toString(), offset, length});
    }
}

void applyVram(Scene& scene, const Documents& documents, const QJsonArray& loads) {
    for (const QJsonValue& value : loads) {
        const QJsonObject load = value.toObject();
        const QByteArray bytes = vramSource(documents, load.value(QStringLiteral("command")).toInt(),
                                            load.value(QStringLiteral("source")).toString());
        const int at = load.value(QStringLiteral("vram")).toInt() & 0xFFFF;
        const int length = std::min<int>(bytes.size(), kVramSize - at);
        std::copy(bytes.cbegin(), bytes.cbegin() + length, scene.vram.begin() + at);
    }
}

Plane planeOf(const QJsonValue& value) {
    Plane plane;
    const QJsonObject object = value.toObject();
    if (object.isEmpty()) return plane;
    plane.present = true;
    plane.coarse = quint32(object.value(QStringLiteral("coarse")).toDouble());
    plane.chunks = quint32(object.value(QStringLiteral("chunks")).toDouble());
    plane.patterns = quint32(object.value(QStringLiteral("patterns")).toDouble());
    plane.yOffset = object.value(QStringLiteral("y_offset")).toInt();
    return plane;
}

int ramWord(const QByteArray& ram, int offset) {
    if (offset < 0 || offset + 1 >= ram.size()) return 0;
    return (quint8(ram.at(offset)) << 8) | quint8(ram.at(offset + 1));
}

}  // namespace

QRgb cramColor(int word) { return megadrive::cramToArgb(word); }

QVector<quint8> tilePixels(const QByteArray& tile) {
    QVector<quint8> pixels(64, 0);
    for (int at = 0; at < 32 && at < tile.size(); ++at) {
        pixels[at * 2] = quint8(tile.at(at)) >> 4;
        pixels[at * 2 + 1] = quint8(tile.at(at)) & 15;
    }
    return pixels;
}

QByteArray vramSource(const Documents& documents, int command, const QString& label) {
    if (command == 1 || command == 5) {
        const QJsonObject payload =
            findByLabel(documents[Doc::Tiles].value(QStringLiteral("payloads")).toArray(), label);
        const QJsonArray blocks = payload.value(QStringLiteral("blocks")).toArray();
        QByteArray bytes;
        for (const QJsonValue& tile : blocks.at(0).toObject().value(QStringLiteral("tiles")).toArray()) {
            bytes += hexBytes(tile.toString());
        }
        return bytes;
    }
    const QJsonObject payload =
        findByLabel(documents[Doc::Compressed].value(QStringLiteral("payloads")).toArray(), label);
    QByteArray bytes;
    for (const QJsonValue& tile : payload.value(QStringLiteral("tiles")).toArray()) {
        bytes += hexBytes(tile.toString());
    }
    return bytes;
}

void playPaletteCommand(QVector<int>& cram, const QJsonObject& palettes, const QString& label, int offset) {
    const QJsonObject block = findByLabel(palettes.value(QStringLiteral("blocks")).toArray(), label);
    for (const QJsonValue& value : block.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject command = value.toObject();
        if (command.value(QStringLiteral("offset")).toInt() != offset) continue;
        const int first = command.value(QStringLiteral("destination")).toInt() / 2;
        const QJsonArray colours = command.value(QStringLiteral("colours")).toArray();
        for (int index = 0; index < colours.size() && first + index < cram.size(); ++index) {
            cram[first + index] = colours.at(index).toInt();
        }
    }
}

QVector<int> paletteList(const QJsonObject& palettes, const QString& list) {
    QVector<int> cram(64, 0);
    playPaletteCommand(cram, palettes, palettes.value(QStringLiteral("prelude")).toString());
    for (const QJsonValue& value : palettes.value(QStringLiteral("lists")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("label")).toString() != list) continue;
        for (const QJsonValue& entry : item.value(QStringLiteral("commands")).toArray()) {
            const QJsonArray pair = entry.toArray();
            playPaletteCommand(cram, palettes, pair.at(0).toString(), pair.at(1).toInt());
        }
        break;
    }
    return cram;
}

int Scene::region(quint32 base) const {
    QVector<quint32> edges;
    for (const Plane& plane : planes) {
        if (plane.present) edges << plane.coarse << plane.chunks << plane.patterns;
    }
    edges << kTerrainCollisionBuffer;
    quint32 limit = kRamBase + kRamSize - 1;
    for (quint32 edge : edges) {
        if (edge > base) limit = std::min(limit, edge);
    }
    const int at = int(base - kRamBase);
    const int end = int(limit - kRamBase);
    int reach = 0;
    for (const RamWrite& write : writes) {
        if (write.offset >= at && write.offset < end) reach = std::max(reach, write.offset + write.size - at);
    }
    return reach;
}

int Scene::coarseOffset(int index, int x, int y) const {
    const Plane& plane = planes[index];
    if (!plane.present) return -1;
    y += plane.yOffset;
    return int(plane.coarse - kRamBase) + ((y >> 8) & 31) * 32 + ((x >> 8) & 31);
}

int Scene::chunkOffset(int index, int x, int y) const {
    const int coarse = coarseOffset(index, x, y);
    if (coarse < 0 || coarse >= ram.size()) return -1;
    const Plane& plane = planes[index];
    y += plane.yOffset;
    return int(plane.chunks - kRamBase) + quint8(ram.at(coarse)) * 64 + ((y >> 5) & 7) * 8 + ((x >> 5) & 7);
}

int Scene::patternOffset(int index, int x, int y) const {
    const int chunk = chunkOffset(index, x, y);
    if (chunk < 0 || chunk >= ram.size()) return -1;
    const Plane& plane = planes[index];
    y += plane.yOffset;
    return int(plane.patterns - kRamBase) + quint8(ram.at(chunk)) * 32 + ((y >> 3) & 3) * 8 + ((x >> 3) & 3) * 2;
}

int Scene::nameWord(int index, int x, int y) const { return ramWord(ram, patternOffset(index, x, y)); }

bool Scene::defined(int index, int x, int y) const {
    const Plane& plane = planes[index];
    if (!plane.present) return false;
    const int coarse = coarseOffset(index, x, y);
    const int chunk = chunkOffset(index, x, y);
    if (coarse < 0 || chunk < 0 || coarse >= ram.size() || chunk >= ram.size()) return false;
    return quint8(ram.at(coarse)) * 64 < region(plane.chunks) && quint8(ram.at(chunk)) * 32 < region(plane.patterns);
}

QSize Scene::extent(int index) const {
    const Plane& plane = planes[index];
    if (!plane.present) return {};
    int columns = 0, rows = 0;
    for (int row = 0; row < 32; ++row) {
        for (int column = 0; column < 32; ++column) {
            const int x = column * 256, y = row * 256 - plane.yOffset;
            if (defined(index, x, y) && quint8(ram.at(coarseOffset(index, x, y))) != 0) {
                columns = std::max(columns, column + 1);
                rows = std::max(rows, row + 1);
            }
        }
    }
    return {std::max(columns, 2) * 256, std::max(rows, 1) * 256};
}

int Scene::writerOf(int offset) const {
    for (int index = writes.size() - 1; index >= 0; --index) {
        if (offset >= writes[index].offset && offset < writes[index].offset + writes[index].size) return index;
    }
    return -1;
}

int untransform(const RamTransform& transform, int shown) {
    if (transform.rows) {
        const int word = (shown - 0xC180) & 0xFFFF;
        return word <= 0x1FFF ? word : -1;
    }
    const int raw = (shown - 0x6000) & 0xFFFF;
    if (raw & ~0x1FFF) return -1;
    const int tile = raw & 0x7FF;
    return (raw & 0x1800) | (tile ? (tile + 0x100) & 0x7FF : 0);
}

Scene stageScene(const Documents& documents, int stage) {
    Scene scene;
    scene.vram = QByteArray(kVramSize, '\0');
    scene.ram = QByteArray(kRamSize, '\0');
    const QJsonObject stages = documents[Doc::Stages];
    const QJsonObject item = stages.value(QStringLiteral("scenes")).toArray().at(stage).toObject();
    scene.backdrop = stages.value(QStringLiteral("backdrop")).toInt(0x10);
    applyWrites(scene, documents, item.value(QStringLiteral("map_writes")).toArray());
    applyVram(scene, documents, item.value(QStringLiteral("vram")).toArray());
    const QJsonArray planes = item.value(QStringLiteral("planes")).toArray();
    scene.planes[0] = planeOf(planes.at(0));
    scene.planes[1] = planeOf(planes.at(1));
    // Plane B scrolls at a quarter of plane A's speed from the record's
    // cameras; its map is offset by the difference of their Y positions.
    const QJsonObject record = findByLabel(documents[Doc::Levels].value(QStringLiteral("records")).toArray(),
                                           item.value(QStringLiteral("record")).toString());
    const int cameraX = signedWord(record.value(QStringLiteral("camera_x")).toInt());
    const int cameraY = signedWord(record.value(QStringLiteral("camera_y")).toInt());
    const int secondaryX = signedWord(record.value(QStringLiteral("secondary_camera_x")).toInt());
    const int secondaryY = signedWord(record.value(QStringLiteral("secondary_camera_y")).toInt());
    scene.planes[1].yOffset += cameraY - secondaryY;
    scene.planes[1].origin = QPoint(secondaryX - (cameraX >> 2), 0);
    scene.planes[1].shift = 2;
    const QJsonObject palettes = documents[Doc::Palettes];
    scene.cram = paletteList(palettes, item.value(QStringLiteral("palette_list")).toString());
    const QString visual = item.value(QStringLiteral("visual_palette")).toString();
    if (!visual.isEmpty()) playPaletteCommand(scene.cram, palettes, visual);
    return scene;
}

Scene screenScene(const Documents& documents, int index) {
    Scene scene;
    scene.vram = QByteArray(kVramSize, '\0');
    scene.ram = QByteArray(kRamSize, '\0');
    const QJsonObject screen = documents[Doc::Screens].value(QStringLiteral("screens")).toArray().at(index).toObject();
    applyWrites(scene, documents, screen.value(QStringLiteral("map_writes")).toArray());
    for (const QJsonValue& value : screen.value(QStringLiteral("transforms")).toArray()) {
        const QJsonObject transform = value.toObject();
        const int at = int(quint32(transform.value(QStringLiteral("ram")).toString().toUInt(nullptr, 0)) - kRamBase);
        const int words = transform.value(QStringLiteral("groups")).toInt() * 16;
        const bool rows = transform.value(QStringLiteral("routine")).toString() == QLatin1String("adjust-rows");
        scene.transforms.append({at, words, rows});
        for (int word = 0; word < words; ++word) {
            const int offset = at + word * 2;
            if (offset < 0 || offset + 1 >= kRamSize) break;
            int value16 = ramWord(scene.ram, offset);
            if (rows) {
                value16 = ((value16 & 0x1FFF) + 0xC180) & 0xFFFF;
            } else {
                int tile = value16 & 0x7FF;
                if (tile) tile = (tile + 0xFF00) & 0x7FF;
                value16 = ((tile | (value16 & 0x1800)) + 0x6000) & 0xFFFF;
            }
            scene.ram[offset] = char(value16 >> 8);
            scene.ram[offset + 1] = char(value16 & 0xFF);
        }
    }
    applyVram(scene, documents, screen.value(QStringLiteral("vram")).toArray());
    const QJsonArray planes = screen.value(QStringLiteral("planes")).toArray();
    const QJsonArray cameraX = screen.value(QStringLiteral("camera_x")).toArray();
    const QJsonArray lookupY = screen.value(QStringLiteral("lookup_y")).toArray();
    for (int plane = 0; plane < 2; ++plane) {
        scene.planes[plane] = planeOf(planes.at(plane));
        scene.planes[plane].origin = QPoint(cameraX.at(plane).toInt(), lookupY.at(plane).toInt());
    }
    const QJsonObject palette = screen.value(QStringLiteral("palette")).toObject();
    const QJsonObject palettes = documents[Doc::Palettes];
    if (palette.contains(QStringLiteral("list"))) {
        scene.cram = paletteList(palettes, palette.value(QStringLiteral("list")).toString());
    } else {
        scene.cram = QVector<int>(64, 0);
        playPaletteCommand(scene.cram, palettes, palette.value(QStringLiteral("command")).toString());
    }
    return scene;
}

QImage renderScene(const Scene& scene, const QRect& area, bool drawA, bool drawB) {
    QImage image(area.size(), QImage::Format_ARGB32);
    const QRgb backdrop = cramColor(scene.cram.value(scene.backdrop));
    image.fill(backdrop);
    const auto pixelOf = [&scene](int plane, int x, int y, int* priority) -> int {
        if (!scene.defined(plane, x, y)) return 0;
        const megadrive::TileWord word = megadrive::decodeTileWord(scene.nameWord(plane, x, y));
        const int px = word.hflip ? 7 - (x & 7) : (x & 7);
        const int py =
            word.vflip ? 7 - ((y + scene.planes[plane].yOffset) & 7) : ((y + scene.planes[plane].yOffset) & 7);
        const int at = word.pattern * 32 + py * 4 + px / 2;
        if (at >= scene.vram.size()) return 0;
        const int value = (px & 1) ? quint8(scene.vram.at(at)) & 15 : quint8(scene.vram.at(at)) >> 4;
        *priority = word.priority ? 1 : 0;
        return value ? word.palette * 16 + value : 0;
    };
    for (int sy = 0; sy < area.height(); ++sy) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(sy));
        for (int sx = 0; sx < area.width(); ++sx) {
            const int x = area.x() + sx, y = area.y() + sy;
            int colour[2] = {0, 0}, priority[2] = {0, 0};
            for (int plane = 0; plane < 2; ++plane) {
                if (!scene.planes[plane].present || (plane == 0 ? !drawA : !drawB)) continue;
                const Plane& source = scene.planes[plane];
                colour[plane] =
                    pixelOf(plane, (x >> source.shift) + source.origin.x(), y + source.origin.y(), &priority[plane]);
            }
            int chosen = 0;
            for (int pass = 1; pass >= 0 && !chosen; --pass) {
                for (int plane = 0; plane < 2 && !chosen; ++plane) {
                    if (colour[plane] && priority[plane] == pass) chosen = colour[plane];
                }
            }
            if (chosen) line[sx] = cramColor(scene.cram.value(chosen));
        }
    }
    return image;
}

namespace {

// Draws one name-table word's tile at (x, y) of `image`.
void drawWord(QImage& image, const Scene& scene, int word, int x, int y) {
    const megadrive::TileWord tile = megadrive::decodeTileWord(word);
    const QVector<quint8> pixels = tilePixels(scene.vram.mid(tile.pattern * 32, 32));
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const int value = pixels[(tile.vflip ? 7 - py : py) * 8 + (tile.hflip ? 7 - px : px)];
            if (value && x + px < image.width() && y + py < image.height()) {
                image.setPixel(x + px, y + py, cramColor(scene.cram.value(tile.palette * 16 + value)));
            }
        }
    }
}

}  // namespace

int chunkCount(const Scene& scene, int plane) {
    return scene.planes[plane].present ? scene.region(scene.planes[plane].chunks) / 64 : 0;
}

int patternCount(const Scene& scene, int plane) {
    return scene.planes[plane].present ? scene.region(scene.planes[plane].patterns) / 32 : 0;
}

QImage patternImage(const Scene& scene, int plane, int pattern) {
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const int base = int(scene.planes[plane].patterns - kRamBase) + pattern * 32;
    for (int cell = 0; cell < 16; ++cell) {
        drawWord(image, scene, ramWord(scene.ram, base + cell * 2), (cell % 4) * 8, (cell / 4) * 8);
    }
    return image;
}

QImage chunkImage(const Scene& scene, int plane, int chunk) {
    QImage image(256, 256, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const int base = int(scene.planes[plane].chunks - kRamBase) + chunk * 64;
    const int patterns = int(scene.planes[plane].patterns - kRamBase);
    for (int slot = 0; slot < 64; ++slot) {
        if (base + slot >= scene.ram.size()) break;
        const int pattern = quint8(scene.ram.at(base + slot));
        for (int cell = 0; cell < 16; ++cell) {
            drawWord(image, scene, ramWord(scene.ram, patterns + pattern * 32 + cell * 2),
                     (slot % 8) * 32 + (cell % 4) * 8, (slot / 8) * 32 + (cell / 4) * 8);
        }
    }
    return image;
}

QImage tileImage(const QByteArray& vram, int tile, const QVector<int>& cram, int line) {
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QVector<quint8> pixels = tilePixels(vram.mid(tile * 32, 32));
    for (int index = 0; index < 64; ++index) {
        if (pixels[index]) image.setPixel(index % 8, index / 8, cramColor(cram.value(line * 16 + pixels[index])));
    }
    return image;
}

}  // namespace rl::alien_soldier
