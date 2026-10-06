#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QVector>

#include <optional>

namespace rl::pacman {

class PacmanSession;

// The 27x22 maze (pacman.maze-rle.v1): paint maze tiles from the sheet; the
// engine's landmarks (tunnel ends, ghost house and door, spawns, power
// pellets) are marked, and the checks the kit runs show as you paint.
class PacmanMazeModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    explicit PacmanMazeModel(PacmanSession* session);

    QString title() const override { return tr("Maze"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;
    QString startPoint() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

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

private:
    QVector<int> grid() const;
    bool commit(const QVector<int>& grid, const QString& text, QString* error);

    PacmanSession* session_;
    int tile_ = 0x03;
    int cell_ = -1;
    std::optional<QVector<int>> stroke_;
    bool strokeChanged_ = false;
};

}  // namespace rl::pacman
