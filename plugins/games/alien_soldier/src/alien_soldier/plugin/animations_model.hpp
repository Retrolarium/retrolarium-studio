#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One animation script: its entries are the frames (a mapping shown for a
// number of reader calls) and jumps (to another script entry). A frame's
// mapping is drawn piece by piece; a dynamic piece carries its own art, a
// static one draws from the object's tiles, which only the running game
// knows, so it shows as its outline. Pieces move with the mouse and change
// in the inspector; a frame's duration changes there too.
class AnimationsModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    AnimationsModel(AlienSoldierSession* session, const QString& script);

    QString title() const override { return script_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize canvasSize() const override { return {256, 256}; }
    QImage render() const override;
    QPoint origin() const override { return {128, 128}; }
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

private:
    QJsonObject entry(int index) const;
    // The mapping a frame shows: (block index, mapping index) in the mapping
    // document, or block -1.
    QPair<int, int> mappingOf(int frame) const;
    QJsonObject pieceObject(int index) const;

    AlienSoldierSession* session_;
    QString script_;
    int frame_ = 0;
    QString piece_;
    QString paletteList_;
    int line_ = 0;
    QJsonObject dragBefore_;
    quint64 dragRevision_ = 0;
    QPoint dragStart_;
};

}  // namespace rl::alien_soldier
