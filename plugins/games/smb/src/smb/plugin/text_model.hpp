#pragma once

#include "sdk/editor_model.hpp"

namespace rl::smb {

class SmbSession;

// All fixed text of the game in one table: the fixed text fields
// (nes.fixed-text-fields.v1) and the text fields of the screen packets.
// Each has a fixed slot; shorter text is padded with blanks.
class SmbTextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbTextModel(SmbSession* session);

    QString title() const override { return tr("Game text"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    sdk::Budget cellBudget(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    bool setText(int row, const QString& value, QString* error);

    SmbSession* session_;
    int row_ = 0;
};

}  // namespace rl::smb
