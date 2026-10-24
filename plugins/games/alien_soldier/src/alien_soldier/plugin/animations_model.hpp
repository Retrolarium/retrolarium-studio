#pragma once

#include "alien_soldier/plugin/mapping_canvas.hpp"

namespace rl::alien_soldier {

// One animation script: its entries are the frames (a mapping shown for a
// number of reader calls, or held for good) and jumps (to another script
// entry). A frame's mapping draws with its object's tiles and colours where
// the game repository records them; a frame's duration and mapping change in
// the inspector.
class AnimationsModel final : public MappingCanvas {
    Q_OBJECT

public:
    AnimationsModel(AlienSoldierSession* session, const QString& script);

    QString title() const override { return script_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int frame) const override;

protected:
    QPair<int, int> mapping() const override;

private:
    QJsonObject entry(int index) const;

    QString script_;
    int frame_ = 0;
};

}  // namespace rl::alien_soldier
