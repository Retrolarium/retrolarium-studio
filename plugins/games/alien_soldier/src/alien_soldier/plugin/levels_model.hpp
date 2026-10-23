#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One stage slot: both planes as the game draws them at the stage's start
// (plane B with its quarter-speed scroll), its placed objects, and its
// coarse map. Objects move and change in the inspector; the chunks layer
// paints the coarse map's chunk indices (a cell of 8 pixels stands for the
// 256-pixel chunk around it).
class LevelsModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    LevelsModel(AlienSoldierSession* session, int stage);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString startPoint() const override;
    bool isReadOnly() const override;
    QString readOnlyReason() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;
    QString currentLibraryItem() const override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    QJsonObject record() const;
    QString spawnList() const;
    QJsonObject placement(int index) const;
    // The coarse map byte a cell's world position reads, and the payload
    // value it is (-1 when no map document holds it).
    int coarseValueIndex(QPoint cell, QString* payload) const;
    int chunkCount() const;

    AlienSoldierSession* session_;
    int stage_;
    QString layer_ = QStringLiteral("objects");
    QString selected_;
    int brush_ = 0;
    bool planeA_ = true;
    bool planeB_ = true;
    QJsonObject strokeBefore_;
    quint64 strokeRevision_ = 0;
};

}  // namespace rl::alien_soldier
