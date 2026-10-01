#pragma once

#include "flicky/data/graphics_document.hpp"
#include "flicky/data/level_document.hpp"
#include "flicky/data/semantics_document.hpp"
#include "flicky/data/sequences_document.hpp"

#include <QHash>
#include <QImage>
#include <QVector>

#include <array>
#include <memory>
#include <stdexcept>

namespace rl::flicky {

class FlickyLevelSceneError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Source-table-driven 256x224 Mega Drive composition. Documents are shared
// with their sibling Studio tabs, so unsaved tile/palette/mapping edits show
// in the level preview on dependency refresh.
class FlickyLevelSceneRenderer final {
public:
    enum class Layer { Objects, Layout, Background };
    FlickyLevelSceneRenderer(
        QString sourceRoot,
        std::shared_ptr<FlickyGraphicsDocument> graphics,
        std::shared_ptr<FlickySemanticsDocument> semantics,
        std::shared_ptr<FlickySequencesDocument> sequences);

    [[nodiscard]] QImage render(
        const FlickyLevelDocument &levels, int round) const;
    [[nodiscard]] QImage renderLayer(
        const FlickyLevelDocument &levels, int round,
        Layer layer) const;
    [[nodiscard]] QImage renderPlacementPreview(
        const QString &field, int round) const;
    // Settled bonus scene after N frames from MAME's ready transition.
    // Draws source-driven seesaw/Tiger, no-input player, held Chick and thrown
    // Chick motion. Late-phase timing, live score/lives and joypad input remain
    // outside the pixel-exact guarantee.
    [[nodiscard]] QImage renderBonusPhase(
        const FlickyLevelDocument &levels,
        int round, int framesAfterReady) const;
    // Isolated, pixel-accurate sprite preview using the same tiles and CRAM
    // path as the level scene. Round is one-based for the editor control.
    [[nodiscard]] QImage renderMappingPreview(
        const QString &mappingId, int round) const;
    // UI animations point to 3x1 or 3x3 source tile-word tables rather than
    // sprite mappings. Render them through the same 4bpp/CRAM VDP path.
    [[nodiscard]] QImage renderTilemapFramePreview(
        const QString &frameId, int round) const;
    // The 64 CRAM words the game loads for a one-based round.
    [[nodiscard]] std::array<int, 64> paletteForRound(int round) const;

private:
    using Plane = std::array<int, 32 * 28>;
    [[nodiscard]] const QVector<int> &table(const QString &id) const;
    [[nodiscard]] megadrive::Tile tile(int index) const;
    void drawTile(QImage &frame, const std::array<int, 64> &palette,
                  int x, int y, int word, bool transparent,
                  bool wrapX = true) const;
    void drawPlane(QImage &frame, const std::array<int, 64> &palette,
                   const Plane &plane, int offsetX) const;
    void drawMapping(QImage &frame, const std::array<int, 64> &palette,
                     const QString &id, int x, int y,
                     bool objectFlipped = false, bool wrapX = true) const;
    void drawBonusText(QImage &frame, const std::array<int, 64> &palette,
                       const QString &id, int vramAddress) const;
    void drawBonusString(QImage &frame, const std::array<int, 64> &palette,
                         const QString &value, int vramAddress) const;
    void drawBonusPriorityText(QImage &frame,
                               const std::array<int, 64> &palette,
                               int roundNumber) const;
    static void writeTilemap(Plane &plane, int x, int y,
                             const QVector<int> &words, int width, int height);
    [[nodiscard]] int groundWord(const FlickyCollisionGrid &grid,
                                 int x, int y, int theme) const;
    static QRgb mdColour(int word);

    QString sourceRoot_;
    QHash<QString, QVector<int>> tables_;
    mutable QHash<int, megadrive::Tile> tileCache_;
    std::array<int, 64> sharedPalette_ {};
    std::shared_ptr<FlickyGraphicsDocument> graphics_;
    std::shared_ptr<FlickySemanticsDocument> semantics_;
    std::shared_ptr<FlickySequencesDocument> sequences_;
};

} // namespace rl::flicky
