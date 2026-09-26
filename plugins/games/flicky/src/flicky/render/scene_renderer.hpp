#pragma once

#include <retrolarium/flicky_graphics_document.hpp>
#include <retrolarium/flicky_level_document.hpp>
#include <retrolarium/flicky_semantics_document.hpp>
#include <retrolarium/flicky_sequences_document.hpp>

#include <QHash>
#include <QImage>
#include <QVector>

#include <array>
#include <memory>
#include <stdexcept>

class FlickyLevelSceneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Source-table-driven 256x224 Mega Drive composition. Documents are shared
// with their sibling Studio tabs, so unsaved tile/palette/mapping edits show
// in the level preview on dependency refresh.
class FlickyLevelSceneRenderer final {
public:
    FlickyLevelSceneRenderer(
        QString sourceRoot,
        std::shared_ptr<retrolarium::FlickyGraphicsDocument> graphics,
        std::shared_ptr<retrolarium::FlickySemanticsDocument> semantics,
        std::shared_ptr<retrolarium::FlickySequencesDocument> sequences);

    [[nodiscard]] QImage render(
        const retrolarium::FlickyLevelDocument &levels, int round) const;
    // Isolated, pixel-accurate sprite preview using the same tiles and CRAM
    // path as the level scene. Round is one-based for the editor control.
    [[nodiscard]] QImage renderMappingPreview(
        const QString &mappingId, int round) const;
    // UI animations point to 3x1 or 3x3 source tile-word tables rather than
    // sprite mappings. Render them through the same 4bpp/CRAM VDP path.
    [[nodiscard]] QImage renderTilemapFramePreview(
        const QString &frameId, int round) const;

private:
    using Plane = std::array<int, 32 * 28>;
    [[nodiscard]] const QVector<int> &table(const QString &id) const;
    [[nodiscard]] std::array<int, 64> paletteForRound(int round) const;
    [[nodiscard]] retrolarium::FlickyTile tile(int index) const;
    void drawTile(QImage &frame, const std::array<int, 64> &palette,
                  int x, int y, int word, bool transparent) const;
    void drawPlane(QImage &frame, const std::array<int, 64> &palette,
                   const Plane &plane, int offsetX) const;
    void drawMapping(QImage &frame, const std::array<int, 64> &palette,
                     const QString &id, int x, int y) const;
    static void writeTilemap(Plane &plane, int x, int y,
                             const QVector<int> &words, int width, int height);
    [[nodiscard]] int groundWord(const retrolarium::FlickyCollisionGrid &grid,
                                 int x, int y, int theme) const;
    static QRgb mdColour(int word);

    QString sourceRoot_;
    QHash<QString, QVector<int>> tables_;
    mutable QHash<int, retrolarium::FlickyTile> tileCache_;
    std::array<int, 64> sharedPalette_ {};
    std::shared_ptr<retrolarium::FlickyGraphicsDocument> graphics_;
    std::shared_ptr<retrolarium::FlickySemanticsDocument> semantics_;
    std::shared_ptr<retrolarium::FlickySequencesDocument> sequences_;
};
