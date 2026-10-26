#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One palette command of a block, shown where it lands: colour RAM's four
// lines of sixteen, the colours the command writes in place (from its
// destination on) and the rest left empty. Colours change channel by
// channel; the bits CRAM ignores stay as the game stores them. The samples
// show a stage or screen whose palette list plays the block.
class PalettesModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    // `command` counts the block's commands from 0.
    PalettesModel(AlienSoldierSession* session, const QString& label, int command);

    QString title() const override;
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
    // The command, and the index of its colour at a CRAM line and entry
    // (-1 where it writes none).
    QJsonObject command() const;
    int colourAt(QPoint entry) const;
    int word(QPoint entry) const;

    AlienSoldierSession* session_;
    QString label_;
    int command_;
    QPoint entry_;
};

}  // namespace rl::alien_soldier
