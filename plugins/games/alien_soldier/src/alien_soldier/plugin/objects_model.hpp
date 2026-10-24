#pragma once

#include "alien_soldier/plugin/mapping_canvas.hpp"

namespace rl::alien_soldier {

// One block of sprite mappings, an object's frames: each mapping drawn with
// the tiles, attribute and colours the game repository records for the block
// (seen in play), its pieces movable and editable like an animation's.
class ObjectsModel final : public MappingCanvas {
    Q_OBJECT

public:
    ObjectsModel(AlienSoldierSession* session, const QString& block);

    QString title() const override { return block_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::CompositionFrame> frames() const override;
    int currentFrame() const override { return frame_; }
    void setCurrentFrame(int frame) override;
    int frameDurationMs(int) const override { return 200; }

protected:
    QPair<int, int> mapping() const override;

private:
    QString block_;
    int frame_ = 0;
};

}  // namespace rl::alien_soldier
