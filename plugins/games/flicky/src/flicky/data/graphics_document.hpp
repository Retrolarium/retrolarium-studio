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

// The ten physical assets from Flicky's graphics/graphics.json. This native
// model edits decoded pixels while retaining the source builder's capacity and
// untouched non-tile records. Nemesis/Enigma recompression is a separate gate.
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

private:
    static int assetIndex(const QString &assetId);
    static void requireTileIndex(int asset, int tileIndex);
    [[nodiscard]] FlickyTile tileAt(int asset, int tileIndex) const;
    void replaceTile(int asset, int tileIndex, const FlickyTile &tile);

    QJsonObject root_;
    QJsonObject baseline_;
};

} // namespace retrolarium
