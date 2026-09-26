#pragma once

#include <retrolarium/flicky_tiles.hpp>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <stdexcept>

namespace retrolarium {

class FlickyGraphicsDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct FlickyCompactPaletteEntry final {
    int index = 0;
    int colour = 0;
    bool last = false;
    bool operator==(const FlickyCompactPaletteEntry &other) const noexcept
    {
        return index == other.index && colour == other.colour
            && last == other.last;
    }
};

// The ten physical assets from Flicky's graphics/graphics.json. This native
// model edits decoded pixels, the compact Sega CRAM entries and the 12x4
// Enigma tilemap while retaining the source builder's slot-capacity gate.
class FlickyGraphicsDocument final {
public:
    static FlickyGraphicsDocument load(const QString &path);
    static FlickyGraphicsDocument fromJson(const QJsonObject &root);

    [[nodiscard]] QJsonObject toJson() const;
    void save(const QString &path) const;
    [[nodiscard]] QStringList tileAssetIds() const;
    [[nodiscard]] int tileCount(const QString &assetId) const;
    [[nodiscard]] bool isFont(const QString &assetId) const;
    [[nodiscard]] FlickyTile tile(const QString &assetId, int tileIndex) const;
    [[nodiscard]] FlickyTile baselineTile(
        const QString &assetId, int tileIndex) const;
    [[nodiscard]] int pixel(const QString &assetId, int tileIndex,
                            int x, int y) const;
    void setPixel(const QString &assetId, int tileIndex,
                  int x, int y, int value);
    void restoreTile(const QString &assetId, int tileIndex);
    [[nodiscard]] FlickyCompactPaletteEntry segaPaletteEntry(int entry) const;
    [[nodiscard]] FlickyCompactPaletteEntry baselineSegaPaletteEntry(int entry) const;
    void setSegaPaletteEntry(int entry, int cramIndex, int colour);
    [[nodiscard]] int segaTilemapWord(int cell) const;
    [[nodiscard]] int baselineSegaTilemapWord(int cell) const;
    void setSegaTilemapWord(int cell, int word);

private:
    static int assetIndex(const QString &assetId);
    static void requireTileIndex(int asset, int tileIndex);
    [[nodiscard]] FlickyTile tileAt(int asset, int tileIndex) const;
    void replaceTile(int asset, int tileIndex, const FlickyTile &tile);

    QJsonObject root_;
    QJsonObject baseline_;
};

} // namespace retrolarium
