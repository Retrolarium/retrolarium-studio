#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// A block set's chunks for one plane, eight to a row, drawn with the tiles
// and colours of the set's first stage: each 256-pixel chunk is 8x8
// patterns, and painting a cell puts the brush's pattern (32x32, four tiles
// by four) into that slot of the chunk.
class MetatilesModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    MetatilesModel(AlienSoldierSession* session, int set, bool planeB);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    bool isReadOnly() const override;
    QString readOnlyReason() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {32, 32}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;
    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;
    QString currentLibraryItem() const override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

private:
    int plane() const { return plane_; }
    // The RAM offset of a cell's chunk slot, or -1.
    int slotOffset(QPoint cell) const;

    AlienSoldierSession* session_;
    int set_;
    int stage_ = 0;
    int plane_;
    int brush_ = 0;
    QJsonObject strokeBefore_;
    quint64 strokeRevision_ = 0;
};

}  // namespace rl::alien_soldier
