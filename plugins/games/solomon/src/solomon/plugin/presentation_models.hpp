#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QJsonObject>

namespace rl::solomon {

class SolomonSession;

// Where a title-stream literal writes each of its tiles: name-table cell
// (row * 32 + column of the $2800 table) to {token, tile index}.
QHash<int, QPair<int, int>> titleWriters(const QJsonObject& presentation, int stream);

// One packed title stream ("record", "logo") on the 32x30 name table the
// title screen is drawn in. The cells its literal runs write can be
// painted with tiles $80-$FF of the title patterns; the other stream shows
// faintly for context. The stream keeps its byte size.
class SolomonTitleModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SolomonTitleModel(SolomonSession* session, int stream);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override { return {32, 30}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

private:
    QJsonObject presentation() const;
    QImage tile(int value, bool faint) const;

    SolomonSession* session_;
    int stream_;
    int brush_ = 0x80;
    QPoint cell_{-1, -1};
    bool stroking_ = false;
    QJsonObject draft_;
};

// The attract demo: 34 steps of held buttons and how many frames each lasts.
class SolomonDemoModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SolomonDemoModel(SolomonSession* session);

    QString title() const override { return QStringLiteral("Attract demo"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    SolomonSession* session_;
    int row_ = 0;
};

}  // namespace rl::solomon
