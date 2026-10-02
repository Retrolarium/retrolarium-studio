#pragma once

#include "sdk/editor_model.hpp"
#include "smb/data/resource_document.hpp"

#include <optional>

namespace rl::smb {

class SmbSession;

// The 16x16 background blocks of one palette group, eight to a row. Each
// block is four 8x8 patterns; paint a quadrant with the library pattern or
// type its number. Every level that places the block changes with it.
class SmbMetatileModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SmbMetatileModel(SmbSession* session, int group);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
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
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

    static constexpr int kPerRow = 8;

private:
    int count() const;
    int encoded(int index) const { return (group_ << 6) | index; }
    // The block and quadrant (row-major) under a cell; {-1, -1} for none.
    QPair<int, int> blockAt(QPoint cell) const;
    int tileOf(const SmbResourceDocument& document, int index, int quadrant) const;
    sdk::Library library() const;

    SmbSession* session_;
    int group_;
    QString packet_;
    int selected_ = -1;     // block index within the group
    int tile_ = 0x24;

    std::optional<SmbResourceDocument> before_;
    quint64 beforeRevision_ = 0;
    std::optional<SmbResourceDocument> stroke_;
    bool strokeChanged_ = false;

    mutable QVector<sdk::LibraryItem> library_;
    mutable QString libraryKey_;
};

}  // namespace rl::smb
