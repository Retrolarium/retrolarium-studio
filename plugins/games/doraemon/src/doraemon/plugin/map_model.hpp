#pragma once

#include "doraemon/render/painter.hpp"
#include "sdk/editor_model.hpp"

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

#include <optional>

namespace rl::doraemon {

class DoraemonSession;

// A World 1 / World 3 map (doraemon.hierarchy.v1): paint 32x32 big blocks
// from the world's sheet. Each area is drawn in the colours the game gives
// it; World 3's rooms can be given another of the world's presets.
class DoraemonMapModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    DoraemonMapModel(DoraemonSession* session, World world, QString map, QString label);

    QString title() const override { return label_; }
    QString startPoint() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    // Blocks are painted in 32x32 cells; World 1's objects sit in 8x8 cells.
    QSize gridSize() const override;
    QSize cellSize() const override;
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return block_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    // World 1's placement list for this map (world1-city-objects /
    // world1-underground-objects); empty for World 3.
    QString objectsId() const;
    QJsonArray records() const;
    bool commitRecords(const QJsonArray& records, const QString& text, QString* error);
    // A placement's sprite in its area's colours, where it is drawn.
    QImage objectImage(const QJsonObject& record, QPoint* topLeft) const;
    QString objectName(int type) const;
    QStringList typeChoices() const;
    sdk::Inspection inspectObject() const;
    bool setObjectField(const QString& key, const QVariant& value, QString* error);
    // World 3's spawn schedule (world3-schedule): four channels a room, each
    // shown in its room's top-left corner, the type's sprite with its count
    // (an inactive channel as a faint slot).
    QJsonObject scheduleChannel(int slot) const;  // slot = room * 4 + channel
    QImage scheduleImage(int slot, QRect* pixels) const;
    sdk::Inspection inspectSchedule() const;
    bool setScheduleField(const QString& key, const QVariant& value, QString* error);
    bool objectLayer() const { return layer_ == QLatin1String("objects"); }
    // World 1's playtest start on this map, in big blocks; (-1, -1) when
    // it is not here.
    QPoint startCell() const;
    QVector<int> cells() const;
    bool commit(const QVector<int>& cells, const QString& text, QString* error);
    int presetAt(int cell) const;

    DoraemonSession* session_;
    World world_;
    QString map_;
    QString label_;
    int width_ = 0;
    int height_ = 0;
    int block_ = 0;
    int cell_ = -1;
    QHash<int, int> stroke_;  // cell -> painted block
    QString layer_ = QStringLiteral("blocks");
    int object_ = -1;                    // selected placement
    bool startSelected_ = false;         // the playtest start is selected
    int scheduled_ = -1;                 // selected World 3 schedule slot
    std::optional<QJsonArray> dragged_;  // the list while a placement is dragged
    mutable QImage base_;
    mutable quint64 baseRevision_ = ~quint64(0);
};

}  // namespace rl::doraemon
