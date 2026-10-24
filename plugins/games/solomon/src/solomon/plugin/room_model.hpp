#pragma once

#include "sdk/editor_model.hpp"
#include "solomon/data/documents.hpp"

#include <optional>

namespace rl::solomon {

class SolomonSession;

// One Solomon's Key room on the map editor: 16x12 cells of 16x16 pixels.
// Blocks paints the brown and white block planes; Items moves, adds and
// removes the item stream's items, the door, key and mirrors; Enemies the
// enemy stream and the player's start.
class SolomonRoomModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SolomonRoomModel(SolomonSession* session, int room);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString startPoint() const override;
    QString startLibraryItem() const override { return QStringLiteral("special:start"); }
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    // Column 15 lies behind the right wall: the grid is the 15 played,
    // between the walls and above the floor render() draws around them.
    QSize gridSize() const override { return {kWidth - 1, kHeight}; }
    QMargins renderMargins() const override { return {8, 0, 8, 16}; }
    QSize cellSize() const override { return {16, 16}; }
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

private:
    const SolomonRooms& rooms() const;
    int bank() const;
    int group() const;
    sdk::Library itemLibrary() const;
    sdk::Library enemyLibrary() const;
    QImage itemIcon(int type) const;
    // Moves a record of the room to a cell in `rooms`; false when nothing moved.
    bool moveTo(SolomonRooms& rooms, const QString& id, QPoint cell) const;
    bool commit(const SolomonRooms& rooms, const QString& text, QString* error);
    QString objectLabel(const QString& id) const;

    SolomonSession* session_;
    int room_;
    QString layer_ = QStringLiteral("items");
    int brush_ = 1;
    int itemType_ = 0x18;
    int enemyType_ = 0x18;
    QString selected_;
    // A stroke or drag edits this copy and writes the document once.
    std::optional<SolomonRooms> draft_;

    mutable QVector<sdk::LibraryItem> items_;
    mutable QVector<sdk::LibraryItem> enemies_;
    mutable QString libraryKey_;
};

// The Playtest view: the build in MAME, entering the room open in Rooms.
class SolomonPlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit SolomonPlaytestModel(SolomonSession* session);

    QString title() const override { return tr("Build and run"); }
    sdk::Inspection inspect() const override;
    QSize screenSize() const override { return {256, 240}; }
    QString coreLabel() const override;
    QString startLabel() const override;
    void play() override;
    bool isRecording() const override;

private:
    SolomonSession* session_;
};

}  // namespace rl::solomon
