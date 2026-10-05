#pragma once

#include "nes/sound_effect_model.hpp"
#include "sdk/editor_model.hpp"

#include <memory>

namespace rl::smb {

class SmbSession;

// The bundle resource of the effect tables (smb.sfx-tables.v1).
inline const QString kSfxTables = QStringLiteral("sfx-tables");

// The sound effects SoundEngine starts from its three queues, in the order
// they come up in play.
QVector<nes::SoundEffect> soundEffects();
// The effect tables of the session's resource document; edits are undo
// steps of the session.
std::shared_ptr<nes::SoundEffectData> soundEffectData(SmbSession* session);

// The four effect tables byte by byte: the 1-up, power-up grab and power-up
// reveal / vine notes (offsets into the music period table, shown with the
// note they play on square 2) and the brick shatter noise periods.
class SmbSfxTablesModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SmbSfxTablesModel(SmbSession* session);

    QString title() const override { return QStringLiteral("Sound effect tables"); }
    sdk::Inspection inspect() const override;
    sdk::Budget budget() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;
    QStringList cellChoices(int row, int column) const override;

private:
    struct Row {
        int record = -1;
        int index = -1;
    };
    Row rowAt(int row) const;
    QString valueLabel(int record, int value) const;

    SmbSession* session_;
    int row_ = 0;
};

}  // namespace rl::smb
