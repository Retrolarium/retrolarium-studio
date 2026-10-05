#pragma once

#include "doraemon/render/painter.hpp"
#include "sdk/editor_model.hpp"

#include <QHash>
#include <QImage>

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
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override { return {width_, height_}; }
    QSize cellSize() const override { return {32, 32}; }
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

private:
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
    QHash<int, int> stroke_;        // cell -> painted block
    mutable QImage base_;
    mutable quint64 baseRevision_ = ~quint64(0);
};

}  // namespace rl::doraemon
