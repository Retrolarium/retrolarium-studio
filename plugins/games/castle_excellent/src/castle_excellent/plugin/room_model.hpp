#pragma once

#include "castle/data/levels.hpp"
#include "sdk/editor_model.hpp"

#include <QHash>

#include <optional>

namespace rl::castle {

class CastleSession;

// One Castle room on the map editor: 20x20 logical cells of 16x8 pixels.
// The Cells layer paints cell types from the sheet beside the room; the
// Objects layer moves, adds and removes the room's static objects and the
// start marker that "Play from selected" puts the player at.
class CastleRoomModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    CastleRoomModel(CastleSession* session, int room);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString startPoint() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {16, 8}; }
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
    bool rightClickPicks() const override { return true; }
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    bool removeObject(const QString& id, QString* error) override;

    static constexpr int kMaximumObjects = 24;

private:
    QVector<int> roomCells() const;
    QVector<CastleObject> roomObjects() const;
    int objectIndex(const QString& id) const;
    sdk::Library objectLibrary() const;
    QString typeLabel(int type) const;
    bool commitObjects(const QVector<CastleObject>& objects, const QString& text, QString* error);
    QRect startCells() const;
    bool hasStart() const;      // the user has put the player in this room

    CastleSession* session_;
    int room_;
    QString layer_ = QStringLiteral("cells");
    int brush_ = 0;
    int objectType_ = 0;
    QString selected_;

    // A stroke paints into this and writes the document once at its end.
    bool stroking_ = false;
    QHash<int, int> pending_;               // cell index: type
    // A drag moves records here and writes once on release.
    std::optional<QVector<CastleObject>> dragged_;
    QPoint dragStartOffset_;                // start marker: pixel offset in its box

    mutable QVector<sdk::LibraryItem> cellSheet_;
    mutable QVector<sdk::LibraryItem> objectSheet_;
    mutable quint64 sheetRevision_ = ~quint64(0);
};

}  // namespace rl::castle
