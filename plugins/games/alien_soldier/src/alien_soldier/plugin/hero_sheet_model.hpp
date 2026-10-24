#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QPair>
#include <QRect>

namespace rl::alien_soldier {

class AlienSoldierSession;

// The player's sprite sheet: each of its dynamic mappings is one frame, put
// together from the art pieces it uploads, all frames on one canvas size. A
// pixel painted on a frame goes into the piece's art under it (the front one
// where pieces overlap), so a piece several frames share changes in all of
// them; outside the pieces nothing can be painted.
class HeroSheetModel final : public sdk::TileModel {
    Q_OBJECT

public:
    explicit HeroSheetModel(AlienSoldierSession* session);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override { return {}; }
    QString statusText() const override;

    int tileCount() const override { return frames_.size(); }
    int penCount() const override { return 16; }
    QVector<QColor> penColors() const override;
    QSize tileSize() const override { return bounds_.size(); }
    QVector<quint8> tilePixels(int tile) const override;
    bool isTileDirty(int tile) const override;
    int currentTile() const override { return frame_; }
    void setCurrentTile(int tile) override;
    int currentPen() const override { return pen_; }
    void setCurrentPen(int pen) override;
    QVector<sdk::Tool> tools() const override;

    void beginStroke() override;
    bool setPixel(QPoint pixel, int pen) override;
    void endStroke() override;

private:
    struct Hit {
        QString art;
        int tile = -1;
        QPoint pixel;  // within the tile
    };
    QJsonArray pieces(int frame) const;
    QString blockLabel(int frame) const;
    // The piece art under a frame pixel (the front piece first).
    Hit hitAt(int frame, QPoint pixel) const;

    AlienSoldierSession* session_;
    QVector<QPair<int, int>> frames_;  // (block, mapping) in the mapping document
    QRect bounds_;                     // all frames' pieces, relative to the object
    int frame_ = 0;
    int pen_ = 1;
    QJsonObject strokeBefore_;
    quint64 strokeRevision_ = 0;
};

}  // namespace rl::alien_soldier
