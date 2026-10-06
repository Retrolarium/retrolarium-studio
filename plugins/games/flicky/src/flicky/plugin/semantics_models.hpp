#pragma once

#include "sdk/editor_model.hpp"

namespace rl::flicky {

class FlickySession;

// One CRAM palette of semantics.json: level, accent or title colors.
class SemanticsPaletteModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    SemanticsPaletteModel(FlickySession* session, const QString& id);

    QString title() const override { return id_; }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return QPoint(entry_, 0); }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Channels; }
    QString pickerTitle() const override;
    QStringList channelNames() const override;
    QVector<int> currentChannels() const override;
    bool setChannel(int channel, int level, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    int cramBase() const;     // first CRAM slot the palette loads into, -1 unknown
    QVector<int> rounds() const;

    FlickySession* session_;
    QString id_;
    int entry_ = 0;
};

// The 52 fixed-capacity strings of semantics.json.
class TextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit TextModel(FlickySession* session);

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
    QString idAt(int row) const;
    bool setText(int row, const QString& value, QString* error);

    FlickySession* session_;
    int row_ = 0;
};

}  // namespace rl::flicky
