#pragma once

#include "alien_soldier/data/bosses.hpp"
#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// A jointed boss (graphics/bosses.json): one of its animations, stepped as
// the game steps it (its keyframes, or every frame), or its pose table. A
// part dragged turns its own joint in the pose shown; the inspector edits a
// pose's channels and an animation's commands. A boss the kit does not read
// as a skeleton (its code draws it) opens empty, saying so.
class BossesModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    // `document` is "anim:<boss>:<label>", "poses:<boss>" or "code:<stage>".
    BossesModel(AlienSoldierSession* session, const QString& document);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    bool isReadOnly() const override { return boss().isEmpty(); }
    QString readOnlyReason() const override;

    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

    // 512 pixels square, wider or taller for a body that reaches further
    // (the Caterpillar's ship), the boss a little below the middle.
    QSize canvasSize() const override;
    QPoint origin() const override;
    QImage render() const override;
    QRect focusRect() const override;

    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override { return piece_; }
    void selectPiece(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragPiece(const QString& id, QPoint offset) override;
    void endDrag() override;

    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;

    // A readable name for an animation label: the boss's name and the
    // closing words left out (Boss_AntroidLeapAttackAPoseCommands of
    // Antroid reads "Leap attack A").
    static QString animationTitle(const QString& label, const QString& boss);

private:
    enum class Mode { Animation, Poses, Code };

    QJsonObject boss() const;
    QJsonArray poseRows() const;
    // The animation's commands, and the frames it steps (cached per edit).
    const PoseRun& run() const;
    const PoseFrames& stepped() const;
    // The pose commands' indices in run() (the keyframes).
    QVector<int> keyframes() const;
    // The channel bytes the current frame shows, and the pose it edits (-1
    // when it edits none: every-frame view, a code boss).
    QVector<int> channelsAt(int frame) const;
    int editedPose() const;
    QVector<PlacedPart> placed(int frame) const;
    bool setPoseChannel(int pose, int channel, int value, QString* error);
    bool setCommand(const QString& key, int value, QString* error);
    void refresh();

    AlienSoldierSession* session_;
    Mode mode_ = Mode::Code;
    QString bossId_;
    QString label_;
    int stage_ = -1;
    int frame_ = 0;
    bool everyFrame_ = false;
    BossStance stance_;
    BossStance stance() const;
    QString piece_;
    mutable quint64 cachedRevision_ = ~0ull;
    mutable PoseRun run_;
    mutable PoseFrames stepped_;
    mutable QRect focus_;
    mutable SpriteContext context_;
    mutable bool contextReady_ = false;
    QJsonObject dragBefore_;
    quint64 dragRevision_ = 0;
    PlacedPart dragPart_;
    QPoint dragParent_;
};

}  // namespace rl::alien_soldier
