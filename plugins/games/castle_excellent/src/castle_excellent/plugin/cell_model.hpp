#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QPair>

#include <array>

namespace rl::castle_excellent {

class CastleExcellentSession;

// The 100 room cells (castle-excellent.room-cell-visuals.v1): each is two background
// patterns side by side, 16x8, with the palette the room loader gives it.
// The sheet shows them ten to a row; painting puts the brush pattern into a
// cell's left or right half.
class CastleExcellentCellModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    explicit CastleExcellentCellModel(CastleExcellentSession* session);

    QString title() const override { return tr("Room cells"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return tile_; }
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

    static constexpr int kPerRow = 10;

private:
    int count() const;
    // The cell and half (0 left, 1 right) under a sheet tile; {-1, -1} for none.
    QPair<int, int> cellAt(QPoint at) const;
    int tileOf(int cell, int half) const;
    int paletteOf(int cell) const;
    static int paletteNumber(const QString& label);
    bool setCellFields(int cell, int left, int right, int palette, const QString& text, QString* error);

    CastleExcellentSession* session_;
    int selected_ = -1;
    int tile_ = 2;
    int libraryPalette_ = 0;
    bool stroking_ = false;
    QHash<int, std::array<int, 2>> pending_;  // cell -> left, right
};

}  // namespace rl::castle_excellent
