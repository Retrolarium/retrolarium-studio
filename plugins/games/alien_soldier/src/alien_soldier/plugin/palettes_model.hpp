#pragma once

#include "sdk/editor_model.hpp"

namespace rl::alien_soldier {

class AlienSoldierSession;

// One palette command block: each command a row of the colours it writes
// into CRAM (from its destination on). Colours change channel by channel;
// the bits CRAM ignores stay as the game stores them. The samples show a
// stage or screen whose palette list plays the block.
class PalettesModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    PalettesModel(AlienSoldierSession* session, const QString& label);

    QString title() const override { return label_; }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Channels; }
    QString pickerTitle() const override;
    QStringList channelNames() const override;
    int channelLevels() const override { return 8; }
    QVector<int> currentChannels() const override;
    bool setChannel(int channel, int level, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    int word(int row, int entry) const;

    AlienSoldierSession* session_;
    QString label_;
    QPoint entry_;
};

}  // namespace rl::alien_soldier
