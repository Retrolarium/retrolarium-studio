#pragma once

#include "doraemon/render/painter.hpp"
#include "sdk/editor_model.hpp"

#include <QVector>

namespace rl::doraemon {

class DoraemonSession;

// A named animation: metasprite indexes of one world's catalog shown one
// after another, with the sprite palette the game gives the actor.
struct DoraemonAnimation {
    World world;
    QString group;          // "Doraemon", "Enemies", "Items", "Creatures", "Frames"
    QString label;
    QVector<int> frames;    // metasprite indexes
    int ticks = 8;
    int palette = 0;        // sprite palette 0-3 (World 2: from the record)
};

const QVector<DoraemonAnimation>& doraemonAnimations();
QString metaspriteId(World world);      // the bundle resource

// One animation on the composition canvas. World 1/3 frames are variable
// records: select a piece to set its tile, offset and flips, drag it, or
// drop a pattern on it; a frame that mirrors another (an alias) is shown
// and edited through its source. World 2 frames are fixed 2x2 records.
class DoraemonAnimationModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    DoraemonAnimationModel(DoraemonSession* session, int animation);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Library sheet() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

    QSize canvasSize() const override { return {160, 160}; }
    QImage render() const override;
    QPoint origin() const override { return {48, 48}; }
    int defaultZoom() const override { return 4; }
    QRect focusRect() const override;
    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override;
    void selectPiece(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragPiece(const QString& id, QPoint offset) override;
    void endDrag() override;
    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

private:
    // A piece as drawn: its place on the canvas, tile and flips.
    struct Placed {
        int piece = 0;      // piece of the record (World 2: quarter)
        int x = 0;
        int y = 0;
        int tile = 0;
        bool flipX = false;
        bool flipY = false;
        int palette = 0;
        bool hidden = false;    // World 2 tile 0: not drawn
    };
    const DoraemonAnimation& animation() const;
    int index() const;          // the metasprite index of the current frame
    // World 1/3: the record an index draws and the alias flip (-1 none).
    int recordOf(int index, int* flip) const;
    QVector<Placed> placed() const;
    QVector<QVector<int>> spritePalettes() const;
    bool editPiece(int piece, int rawY, int rawX, int tile, const QString& text, QString* error);
    bool editWorld2(int quarter, int tile, int attribute, int descriptor, const QString& text, QString* error);

    DoraemonSession* session_;
    int index_;
    int frame_ = 0;
    int selected_ = -1;
    int preset_ = 0;
    int palette_;           // shown sprite palette (World 1/3)
    QPoint dragStart_;
    QPoint dragAt_{-1, -1};
    int dragPiece_ = -1;
    bool dragging_ = false;
};

}  // namespace rl::doraemon
