#include "flicky/data/graphics_document.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <array>

namespace rl::flicky {
namespace {

struct AssetSpec final {
    const char* id;
    const char* kind;
    int count;
    int capacity;
};

constexpr std::array<AssetSpec, 10> specs{{
    {"sega_tiles", "tiles_4bpp", 48, 828},
    {"level_tiles", "tiles_4bpp", 343, 6052},
    {"exit_tiles", "tiles_4bpp", 9, 128},
    {"sprite_tiles", "tiles_4bpp", 303, 4458},
    {"score_tiles", "tiles_4bpp", 83, 1456},
    {"logo_tiles", "tiles_4bpp", 57, 680},
    {"japanese_font", "tiles_1bpp", 179, 1432},
    {"latin_font", "tiles_1bpp", 43, 344},
    {"sega_palette", "palette_compact", 10, 20},
    {"sega_tilemap", "enigma_tilemap", 48, 10},
}};

[[noreturn]] void fail(const QString& message) { throw FlickyGraphicsDocumentError(message.toStdString()); }

int pixelValue(const QJsonValue& value, int maximum) {
    if (!value.isDouble()) fail(QStringLiteral("Flicky tile pixel must be an integer"));
    const double number = value.toDouble();
    const int result = value.toInt(-1);
    if (number != result || result < 0 || result > maximum) {
        fail(QStringLiteral("Flicky tile pixel is outside its bit depth"));
    }
    return result;
}

megadrive::Tile parseTile(const QJsonValue& value, int maximum) {
    megadrive::Tile tile{};
    const auto rows = value.toArray();
    if (rows.size() != 8) fail(QStringLiteral("Flicky tile must have eight rows"));
    for (int y = 0; y < 8; ++y) {
        const auto row = rows.at(y).toArray();
        if (row.size() != 8) fail(QStringLiteral("Flicky tile row must have eight pixels"));
        for (int x = 0; x < 8; ++x) {
            tile[static_cast<std::size_t>(y * 8 + x)] = static_cast<std::uint8_t>(pixelValue(row.at(x), maximum));
        }
    }
    return tile;
}

QJsonArray tileToJson(const megadrive::Tile& tile) {
    QJsonArray rows;
    for (int y = 0; y < 8; ++y) {
        QJsonArray row;
        for (int x = 0; x < 8; ++x) {
            row.append(static_cast<int>(tile[static_cast<std::size_t>(y * 8 + x)]));
        }
        rows.append(row);
    }
    return rows;
}

QJsonArray assetArray(const QJsonObject& root) { return root.value(QStringLiteral("assets")).toArray(); }

FlickyCompactPaletteEntry parseSegaPaletteEntry(const QJsonValue& value, int position) {
    const auto entry = value.toObject();
    const int index = pixelValue(entry.value(QStringLiteral("index")), 63);
    const int colour = pixelValue(entry.value(QStringLiteral("colour")), 0xEEE);
    const auto last = entry.value(QStringLiteral("last"));
    if ((colour & ~0xEEE) != 0 || !last.isBool() || last.toBool() != (position == 9)) {
        fail(QStringLiteral("invalid Flicky Sega compact CRAM entry"));
    }
    return {index, colour, last.toBool()};
}

void requireSegaEntry(int entry) {
    if (entry < 0 || entry >= 10) fail(QStringLiteral("Flicky Sega palette entry must be 0..9"));
}

void requireSegaCell(int cell) {
    if (cell < 0 || cell >= 48) fail(QStringLiteral("Flicky Sega tilemap cell must be 0..47"));
}

}  // namespace

FlickyGraphicsDocument FlickyGraphicsDocument::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open Flicky graphics document: %1").arg(path));
    }
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        fail(QStringLiteral("invalid Flicky graphics JSON: %1").arg(parseError.errorString()));
    }
    return fromJson(json.object());
}

FlickyGraphicsDocument FlickyGraphicsDocument::fromJson(const QJsonObject& root) {
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1 ||
        root.value(QStringLiteral("profile")).toString() != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky graphics schema or profile"));
    }
    const auto assets = assetArray(root);
    if (assets.size() != static_cast<int>(specs.size())) {
        fail(QStringLiteral("Flicky graphics need exactly ten assets"));
    }
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto asset = assets.at(static_cast<int>(index)).toObject();
        const auto& spec = specs[index];
        if (asset.value(QStringLiteral("id")).toString() != QString::fromLatin1(spec.id) ||
            asset.value(QStringLiteral("kind")).toString() != QString::fromLatin1(spec.kind) ||
            asset.value(QStringLiteral("capacity")).toInt(-1) != spec.capacity) {
            fail(QStringLiteral("Flicky graphics asset identity or capacity differs"));
        }
        if (index < 8) {
            const auto tiles = asset.value(QStringLiteral("tiles")).toArray();
            if (tiles.size() != spec.count) {
                fail(QStringLiteral("Flicky tile count differs for %1").arg(QString::fromLatin1(spec.id)));
            }
            const int maximum = index < 6 ? 15 : 1;
            for (const auto& tile : tiles) (void)parseTile(tile, maximum);
        } else if (index == 8) {
            const auto entries = asset.value(QStringLiteral("entries")).toArray();
            if (entries.size() != 10) {
                fail(QStringLiteral("Flicky compact palette needs ten entries"));
            }
            for (int entry = 0; entry < 10; ++entry) (void)parseSegaPaletteEntry(entries.at(entry), entry);
        } else if (asset.value(QStringLiteral("width")).toInt(-1) != 12 ||
                   asset.value(QStringLiteral("height")).toInt(-1) != 4 ||
                   asset.value(QStringLiteral("words")).toArray().size() != 48) {
            fail(QStringLiteral("Flicky Sega tilemap must be 12x4"));
        } else {
            for (const auto& word : asset.value(QStringLiteral("words")).toArray()) (void)pixelValue(word, 0xFFFF);
        }
    }
    FlickyGraphicsDocument document;
    document.root_ = root;
    document.baseline_ = root;
    return document;
}

QJsonObject FlickyGraphicsDocument::toJson() const { return root_; }

void FlickyGraphicsDocument::save(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot save Flicky graphics document: %1").arg(path));
    }
    const auto bytes = QJsonDocument(root_).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        fail(QStringLiteral("cannot commit Flicky graphics document: %1").arg(path));
    }
}

int FlickyGraphicsDocument::assetIndex(const QString& assetId) {
    for (int index = 0; index < 8; ++index) {
        if (assetId == QString::fromLatin1(specs[static_cast<std::size_t>(index)].id)) {
            return index;
        }
    }
    fail(QStringLiteral("unknown Flicky tile asset: %1").arg(assetId));
}

void FlickyGraphicsDocument::requireTileIndex(int asset, int tileIndex) {
    if (tileIndex < 0 || tileIndex >= specs[static_cast<std::size_t>(asset)].count) {
        fail(QStringLiteral("Flicky tile index is outside its bank"));
    }
}

QStringList FlickyGraphicsDocument::tileAssetIds() const {
    QStringList result;
    for (int index = 0; index < 8; ++index) {
        result.append(QString::fromLatin1(specs[static_cast<std::size_t>(index)].id));
    }
    return result;
}

int FlickyGraphicsDocument::tileCount(const QString& assetId) const {
    return specs[static_cast<std::size_t>(assetIndex(assetId))].count;
}

bool FlickyGraphicsDocument::isFont(const QString& assetId) const { return assetIndex(assetId) >= 6; }

megadrive::Tile FlickyGraphicsDocument::tileAt(int asset, int tileIndex) const {
    requireTileIndex(asset, tileIndex);
    const auto tiles = assetArray(root_).at(asset).toObject().value(QStringLiteral("tiles")).toArray();
    return parseTile(tiles.at(tileIndex), asset < 6 ? 15 : 1);
}

megadrive::Tile FlickyGraphicsDocument::tile(const QString& assetId, int tileIndex) const {
    return tileAt(assetIndex(assetId), tileIndex);
}

megadrive::Tile FlickyGraphicsDocument::baselineTile(const QString& assetId, int tileIndex) const {
    const int asset = assetIndex(assetId);
    requireTileIndex(asset, tileIndex);
    const auto tiles = assetArray(baseline_).at(asset).toObject().value(QStringLiteral("tiles")).toArray();
    return parseTile(tiles.at(tileIndex), asset < 6 ? 15 : 1);
}

int FlickyGraphicsDocument::pixel(const QString& assetId, int tileIndex, int x, int y) const {
    if (x < 0 || x >= 8 || y < 0 || y >= 8) {
        fail(QStringLiteral("Flicky tile coordinate must be 0..7"));
    }
    return tile(assetId, tileIndex)[static_cast<std::size_t>(y * 8 + x)];
}

void FlickyGraphicsDocument::replaceTile(int asset, int tileIndex, const megadrive::Tile& tile) {
    auto assets = assetArray(root_);
    auto record = assets.at(asset).toObject();
    auto tiles = record.value(QStringLiteral("tiles")).toArray();
    tiles.replace(tileIndex, tileToJson(tile));
    record.insert(QStringLiteral("tiles"), tiles);
    assets.replace(asset, record);
    root_.insert(QStringLiteral("assets"), assets);
}

void FlickyGraphicsDocument::setPixel(const QString& assetId, int tileIndex, int x, int y, int value) {
    const int asset = assetIndex(assetId);
    requireTileIndex(asset, tileIndex);
    if (x < 0 || x >= 8 || y < 0 || y >= 8 || value < 0 || value > (asset < 6 ? 15 : 1)) {
        fail(QStringLiteral("Flicky tile edit is outside its pixel domain"));
    }
    auto pixels = tileAt(asset, tileIndex);
    pixels[static_cast<std::size_t>(y * 8 + x)] = static_cast<std::uint8_t>(value);
    replaceTile(asset, tileIndex, pixels);
}

void FlickyGraphicsDocument::restoreTile(const QString& assetId, int tileIndex) {
    const int asset = assetIndex(assetId);
    requireTileIndex(asset, tileIndex);
    const auto baselineTiles = assetArray(baseline_).at(asset).toObject().value(QStringLiteral("tiles")).toArray();
    replaceTile(asset, tileIndex, parseTile(baselineTiles.at(tileIndex), asset < 6 ? 15 : 1));
}

FlickyCompactPaletteEntry FlickyGraphicsDocument::segaPaletteEntry(int entry) const {
    requireSegaEntry(entry);
    return parseSegaPaletteEntry(
        assetArray(root_).at(8).toObject().value(QStringLiteral("entries")).toArray().at(entry), entry);
}

FlickyCompactPaletteEntry FlickyGraphicsDocument::baselineSegaPaletteEntry(int entry) const {
    requireSegaEntry(entry);
    return parseSegaPaletteEntry(
        assetArray(baseline_).at(8).toObject().value(QStringLiteral("entries")).toArray().at(entry), entry);
}

void FlickyGraphicsDocument::setSegaPaletteEntry(int entry, int cramIndex, int colour) {
    requireSegaEntry(entry);
    if (cramIndex < 0 || cramIndex > 63 || colour < 0 || colour > 0xEEE || (colour & ~0xEEE) != 0)
        fail(QStringLiteral("Flicky Sega CRAM edit is outside its domain"));
    auto assets = assetArray(root_);
    auto record = assets.at(8).toObject();
    auto entries = record.value(QStringLiteral("entries")).toArray();
    auto value = entries.at(entry).toObject();
    value.insert(QStringLiteral("index"), cramIndex);
    value.insert(QStringLiteral("colour"), colour);
    entries.replace(entry, value);
    record.insert(QStringLiteral("entries"), entries);
    assets.replace(8, record);
    root_.insert(QStringLiteral("assets"), assets);
}

int FlickyGraphicsDocument::segaTilemapWord(int cell) const {
    requireSegaCell(cell);
    return pixelValue(assetArray(root_).at(9).toObject().value(QStringLiteral("words")).toArray().at(cell), 0xFFFF);
}

int FlickyGraphicsDocument::baselineSegaTilemapWord(int cell) const {
    requireSegaCell(cell);
    return pixelValue(assetArray(baseline_).at(9).toObject().value(QStringLiteral("words")).toArray().at(cell), 0xFFFF);
}

void FlickyGraphicsDocument::setSegaTilemapWord(int cell, int word) {
    requireSegaCell(cell);
    if (word < 0 || word > 0xFFFF) fail(QStringLiteral("Flicky Sega tilemap word must be 0..65535"));
    auto assets = assetArray(root_);
    auto record = assets.at(9).toObject();
    auto words = record.value(QStringLiteral("words")).toArray();
    words.replace(cell, word);
    record.insert(QStringLiteral("words"), words);
    assets.replace(9, record);
    root_.insert(QStringLiteral("assets"), assets);
}

}  // namespace rl::flicky
