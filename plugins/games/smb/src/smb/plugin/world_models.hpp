#pragma once

#include "sdk/editor_model.hpp"

#include <QPair>

namespace rl::smb {

class SmbSession;

// The world routes: which area each course step of each world plays. The
// number of steps per world is fixed; any area can be the target. Picking
// a row makes its area the start of Play from selected.
class SmbRoutesModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbRoutesModel(SmbSession* session);

    QString title() const override { return tr("World routes"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString startPoint() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;
    QStringList cellChoices(int row, int column) const override;
    QVector<QImage> cellChoiceIcons(int row, int column) const override;

private:
    QPair<int, int> stepAt(int row) const;      // {world, step}, step from 0
    QString areaAt(int row) const;              // "ground_6"
    QString courseLabel(int row) const;         // "1-1", "1-2 · pipe intro"
    bool setArea(int row, const QString& area, QString* error);
    bool setAlternate(int row, bool alternate, QString* error);

    SmbSession* session_;
    int row_ = 0;
};

// The player physics tables: jump and fall gravity, jump speeds, top
// speeds, friction and climbing, byte by byte.
class SmbPhysicsModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbPhysicsModel(SmbSession* session);

    QString title() const override { return tr("Player physics"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    bool setValue(int row, int index, int value, QString* error);

    SmbSession* session_;
    int row_ = 0;
};

}  // namespace rl::smb
