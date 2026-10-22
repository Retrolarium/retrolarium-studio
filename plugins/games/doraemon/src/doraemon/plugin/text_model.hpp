#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QVector>

namespace rl::doraemon {

class DoraemonSession;

// The shell text (retrolarium.doraemon-text) as fixed-width rows: the
// title's text records, GAME OVER and the chapter-help item names, or the
// 380 ending-credit rows. Every row keeps its places; the selected row is
// drawn with the game's own letters (CHR bank 3).
class DoraemonTextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    DoraemonTextModel(DoraemonSession* session, bool credits);

    QString title() const override;
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override { return rows_.size(); }
    QString cell(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    // Where a row's text lives in the document.
    struct Row {
        QString label;
        QString where;    // "game_over", "title", "help", "credits"
        int index = 0;    // record, screen or credits row
        int segment = 0;  // help segment
        int width = 0;
    };
    void readRows();
    QString text(const QJsonObject& document, const Row& row) const;
    QImage drawn(const QString& text, const Row& row) const;

    DoraemonSession* session_;
    bool credits_;
    QVector<Row> rows_;
    int row_ = 0;
};

}  // namespace rl::doraemon
