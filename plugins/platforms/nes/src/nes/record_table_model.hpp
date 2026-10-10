#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace rl::nes {

class BundleOwner;

// A table of byte records (the kit's nes.record-table.v1): a row per record,
// a column per field, as the resource describes them. A field with choices
// edits through a list, a field that may be empty takes "—", and a locked
// cell (code, or a byte another table owns) stays as it is. The kit refuses
// a value out of range and two values for a byte two tables share.
class RecordTableModel final : public sdk::TableModel {
    Q_OBJECT

public:
    RecordTableModel(BundleOwner* owner, const QString& resource, const QString& title, const QString& note = {});

    QString title() const override { return title_; }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;
    QStringList cellChoices(int row, int column) const override;

    // A record or field label in the interface language: "Room 12" through
    // the value "Room %1".
    static QString recordLabel(const QString& label);

private:
    QJsonObject resource(bool saved = false) const;
    QJsonArray fields() const;
    QString display(const QJsonObject& field, const QJsonValue& value) const;
    bool locked(const QJsonObject& field, int row) const;

    BundleOwner* owner_;
    QString resource_;
    QString title_;
    QString note_;
    int row_ = 0;
};

}  // namespace rl::nes
