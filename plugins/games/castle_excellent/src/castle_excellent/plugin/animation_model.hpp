#pragma once

#include "sdk/editor_model.hpp"

#include <QVector>

namespace rl::castle_excellent {

class CastleExcellentSession;

// One frame the code draws: metasprite descriptors from `first`, side by
// side at the renderer's x offsets, for `ticks` frames.
struct CastleExcellentAnimationFrame {
    int first = 0;
    QVector<int> offsets;   // x of each 8x16 sprite
    int ticks = 8;
    bool weapon = false;    // the player's action overlay (tile $CA) follows
    bool flipped = false;   // ... on the left
};

// A player animation (from the source's frame selection) or the frames an
// object type's sprite selector chooses between.
struct CastleExcellentAnimation {
    QString id;             // "player:walk-right", "object:84"
    QString label;
    QString group;          // "Player", "Enemies", "Objects"
    QVector<CastleExcellentAnimationFrame> frames;
};

// The game's animation catalog (its repository's names.json); empty
// without it.
QVector<CastleExcellentAnimation> castleExcellentAnimations();

// An animation on the composition canvas: step or play its frames, select a
// sprite to set its patterns, palette and flip, or drop a pattern pair on
// it. The descriptors are shared: every frame that uses one changes with it.
class CastleExcellentAnimationModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    CastleExcellentAnimationModel(CastleExcellentSession* session, int animation);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Library sheet() const override;

    QSize canvasSize() const override { return {64, 32}; }
    QImage render() const override;
    QPoint origin() const override { return {24, 8}; }
    int defaultZoom() const override { return 6; }
    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override;
    void selectPiece(const QString& id) override;
    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

private:
    const CastleExcellentAnimation& animation() const;
    int tileOf(int descriptor) const;
    int attributesOf(int descriptor) const;
    bool setDescriptor(int descriptor, int tile, int attributes, QString* error);

    CastleExcellentSession* session_;
    int index_;
    CastleExcellentAnimation animation_;   // as the catalog had it when opened
    int frame_ = 0;
    int selected_ = -1;     // descriptor
};

}  // namespace rl::castle_excellent
