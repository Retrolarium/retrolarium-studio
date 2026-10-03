#pragma once

#include "sdk/editor_model.hpp"

namespace rl::castle {

class CastleSession;

// The sub-command menu words (castle.sub-command-text.v1): each record is a
// fixed run of font codes ending in 0. Letters A-Z are codes $71-$8A and
// $FF is a blank.
class CastleTextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    CastleTextModel(CastleSession* session, const QString& resource);

    QString title() const override { return QStringLiteral("Menu text"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    sdk::Budget cellBudget(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

    static QString decode(const QVector<int>& codes);
    // Codes for a record of `size` bytes (the last is the 0 terminator);
    // empty and the reason when the text does not fit or has a character
    // the font lacks.
    static QVector<int> encode(const QString& text, int size, QString* error);

private:
    QVector<int> values(int row) const;
    QVector<int> savedValues(int row) const;
    QString recordId(int row) const;
    bool setText(int row, const QString& text, QString* error);

    CastleSession* session_;
    QString resource_;
    int row_ = 0;
};

// Sound-effect tuning bytes (castle.sfx-parameters.v1): named records of
// period and timing values the sound driver reads.
class CastleSfxModel final : public sdk::TableModel {
    Q_OBJECT

public:
    CastleSfxModel(CastleSession* session, const QString& resource);

    QString title() const override { return QStringLiteral("Sound effect parameters"); }
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    int width() const;

    CastleSession* session_;
    QString resource_;
    int row_ = 0;
};

// The Playtest view: the build in MAME's NES driver.
class CastlePlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit CastlePlaytestModel(CastleSession* session);

    QString title() const override { return QStringLiteral("Build and run"); }
    sdk::Inspection inspect() const override;
    QSize screenSize() const override { return {256, 240}; }
    QString coreLabel() const override { return QStringLiteral("MAME · nes"); }
    QString startLabel() const override;
    void play() override;
    bool isRecording() const override;

private:
    CastleSession* session_;
};

}  // namespace rl::castle
