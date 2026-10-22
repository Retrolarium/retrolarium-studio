#pragma once

#include "doraemon/render/painter.hpp"
#include "sdk/editor_model.hpp"

#include <QHash>
#include <array>

namespace rl::doraemon {

class DoraemonSession;

// A sheet of blocks, sixteen to a row: World 1/3 small blocks (16x16, four
// patterns and an attribute byte), big blocks (32x32, four small blocks),
// or World 2's metatiles (16x16, four patterns, a palette row and a solid
// flag). Painting puts the sheet's piece into one quarter of a block.
class DoraemonBlockModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    enum class Kind { Small, Big, Metatile };

    DoraemonBlockModel(DoraemonSession* session, Kind kind, World world);

    QString title() const override;
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
    QSize cellSize() const override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return piece_; }
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

    static constexpr int kPerRow = 16;

private:
    int count() const;
    QString resourceId() const;
    QString listKey() const;  // "small_blocks", "big_blocks", "tiles"
    // The block and quarter under a grid cell; {-1, -1} outside.
    std::array<int, 2> blockAt(QPoint cell) const;
    std::array<int, 4> quarters(int block) const;
    QImage blockImage(int block) const;
    QImage pieceImage(int piece) const;
    bool commitQuarters(const QHash<int, std::array<int, 4>>& blocks, const QString& text, QString* error);

    DoraemonSession* session_;
    Kind kind_;
    World world_;
    int preset_ = 0;
    int piece_ = 0;
    int selected_ = -1;
    QHash<int, std::array<int, 4>> stroke_;
};

}  // namespace rl::doraemon
