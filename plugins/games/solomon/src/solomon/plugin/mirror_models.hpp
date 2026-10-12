#pragma once

#include "sdk/editor_model.hpp"

namespace rl::solomon {

class SolomonSession;

// The mirror enemy sets every room's mirrors point at: the enemies
// a mirror drops in turn and where it loops back to. They share 42 bytes.
class SolomonMirrorSetsModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SolomonMirrorSetsModel(SolomonSession* session);

    QString title() const override { return tr("Mirror enemy sets"); }
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

// The drop schedules: when a mirror drops, as bits it reads MSB first.
class SolomonSchedulesModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SolomonSchedulesModel(SolomonSession* session);

    QString title() const override { return tr("Mirror drop schedules"); }
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

// "Rooms 3, 7 (mirror 2)": the rooms whose mirrors use a set or schedule.
QString mirrorUsers(const SolomonSession* session, const QString& field, int index);

}  // namespace rl::solomon
