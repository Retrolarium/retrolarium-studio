#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QVector>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One palette command a place plays: a command of a block.
struct PaletteStep {
    QString block;
    int command = 0;
};

// A place the game shows colours, and the commands that set them up there,
// in the order they run: the stages one palette list starts, a screen, a
// boss fight (its stage's list, then its own command), a list no stage or
// screen names, or a command no list plays.
struct PaletteLocation {
    QString id;     // "stages:<list>:<command>", "screen:<id>", "boss:<id>", "list:<label>", "<block>#N"
    QString group;  // "stages", "screens", "bosses", "lists", "commands"
    QString title;
    QVector<PaletteStep> steps;
};

// A place's colours as colour RAM gets them: one swatch row per line a
// command writes, grouped by the command, so a place that plays several
// commands shows all their lines (more than four when two write the same
// line) and lines no command writes are left out. Colours change channel by
// channel; the bits CRAM ignores stay as the game stores them. The preview
// shows the place: the stage at its start, the screen, the boss, or the
// objects that draw with the command.
class PalettesModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    // `document` is a PaletteLocation id; a bare block label opens its
    // first command.
    PalettesModel(AlienSoldierSession* session, const QString& document);

    // Every place, in the order the palette list shows them.
    static QVector<PaletteLocation> locations(const AlienSoldierSession* session);
    // The lines a place's commands write.
    static int lineCount(const QJsonObject& palettes, const QVector<PaletteStep>& steps);

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
    struct Line {
        int step = 0;
        int line = 0;
    };

    PaletteLocation location() const;
    QVector<Line> lines() const;
    QJsonObject command(const PaletteStep& step) const;
    // The index of the colour a row's command writes at an entry (-1 where
    // it writes none).
    int colourAt(QPoint entry) const;
    int word(QPoint entry) const;

    AlienSoldierSession* session_;
    QString document_;
    QPoint entry_;
};

}  // namespace rl::alien_soldier
