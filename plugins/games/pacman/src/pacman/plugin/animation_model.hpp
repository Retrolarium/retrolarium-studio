#pragma once

#include "sdk/editor_model.hpp"

#include <QVector>

namespace rl::pacman {

class PacmanSession;

// One frame an animation shows: a standard or alternate actor frame.
struct PacmanFrame {
    int index = 0;
    bool alternate = false;
    int ticks = 8;
};

// Frames the game steps through for one actor and state (from the actor
// frame map of the source reconstruction). `palette` is the actor's
// sprite palette; `ghost` animations can be shown in each ghost's.
struct PacmanAnimation {
    QString id;
    QString label;
    QString group;  // "Pac-Man", "Ghosts", "Fruit", "Scores", "Intermission", "Frames"
    QVector<PacmanFrame> frames;
    int palette = 0;
    bool ghost = false;
    bool fruit = false;       // the fruit's colour comes from the round
    bool frightened = false;  // sprite palette 1 in the frightened colour
    int fruitIndex = -1;      // the fruit's number in the game's fruit list
};

const QVector<PacmanAnimation>& pacmanAnimations();
// A ghost's name for its palette and an animation's label: the game's names
// (ghosts and fruits in its repository's names.json), else "Ghost 1",
// "Fruit 1".
QString ghostName(int palette);
QString animationLabel(const PacmanAnimation& animation);

// An actor animation on the composition canvas: step or play its frames,
// select a quarter to set its pattern, palette bits, priority and flip, or
// drop a sprite pattern on it. Frames are shared: every animation that
// shows a frame changes with it.
class PacmanAnimationModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    PacmanAnimationModel(PacmanSession* session, int animation);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Library sheet() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

    QSize canvasSize() const override { return {48, 32}; }
    QImage render() const override;
    QPoint origin() const override { return {24, 16}; }
    int defaultZoom() const override { return 8; }
    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override;
    void selectPiece(const QString& id) override;
    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

private:
    const PacmanAnimation& animation() const;
    const PacmanFrame& current() const;
    int palette() const;
    QVector<QVector<int>> sprites() const;
    int tileOf(int quarter) const;
    int attributesOf(int quarter) const;
    bool setQuarter(int quarter, int tile, int attributes, QString* error);

    PacmanSession* session_;
    int index_;
    int frame_ = 0;
    int selected_ = -1;  // quarter 0-3 of the current frame
    int ghost_ = 0;      // shown palette for ghost animations
};

}  // namespace rl::pacman
