#include "alien_soldier/plugin/palettes_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/screens_model.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

QJsonArray commandsOf(const QJsonObject& palettes, const QString& label) {
    const QJsonArray blocks = palettes.value(QStringLiteral("blocks")).toArray();
    return blocks.at(indexOfLabel(blocks, label)).toObject().value(QStringLiteral("commands")).toArray();
}

// Whether a palette list (or the common command every list starts with)
// plays a block.
bool listPlays(const QJsonObject& palettes, const QString& list, const QString& block) {
    if (palettes.value(QStringLiteral("prelude")).toString() == block) return true;
    for (const QJsonValue& value : palettes.value(QStringLiteral("lists")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("label")).toString() != list) continue;
        for (const QJsonValue& entry : item.value(QStringLiteral("commands")).toArray()) {
            if (entry.toArray().at(0).toString() == block) return true;
        }
    }
    return false;
}

}  // namespace

PalettesModel::PalettesModel(AlienSoldierSession* session, const QString& label, int command)
    : PaletteModel(session), session_(session), label_(label), command_(command) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Palettes) emit contentChanged();
    });
}

QString PalettesModel::title() const {
    const int commands = int(commandsOf(session_->doc(Doc::Palettes), label_).size());
    return commands > 1 ? QStringLiteral("%1 · %2").arg(label_).arg(command_ + 1) : label_;
}

QJsonObject PalettesModel::command() const {
    return commandsOf(session_->doc(Doc::Palettes), label_).at(command_).toObject();
}

int PalettesModel::colourAt(QPoint entry) const {
    const QJsonObject item = command();
    const int index = entry.y() * 16 + entry.x() - item.value(QStringLiteral("destination")).toInt() / 2;
    return index >= 0 && index < item.value(QStringLiteral("colours")).toArray().size() ? index : -1;
}

int PalettesModel::word(QPoint entry) const {
    const int index = colourAt(entry);
    return index < 0 ? 0 : command().value(QStringLiteral("colours")).toArray().at(index).toInt();
}

QVector<sdk::PaletteRow> PalettesModel::rows() const {
    QVector<sdk::PaletteRow> result;
    for (int line = 0; line < 4; ++line) {
        sdk::PaletteRow row;
        row.label = tr("Line %1").arg(line);
        for (int entry = 0; entry < 16; ++entry) {
            if (colourAt(QPoint(entry, line)) < 0) {
                // Not this command's: CRAM keeps what others wrote there.
                row.colors.append(QColor(0x1c, 0x1e, 0x24));
                row.labels << QString();
                continue;
            }
            const int value = word(QPoint(entry, line));
            row.colors.append(QColor::fromRgb(cramColor(value)));
            row.labels << QStringLiteral("%1").arg(value, 3, 16, QLatin1Char('0')).toUpper();
        }
        result.append(row);
    }
    return result;
}

void PalettesModel::setCurrentEntry(QPoint entry) {
    entry_ = entry;
    emit selectionChanged();
}

QString PalettesModel::pickerTitle() const { return tr("9-bit RGB · 0–7 per channel"); }

QStringList PalettesModel::channelNames() const { return {tr("Red"), tr("Green"), tr("Blue")}; }

QVector<int> PalettesModel::currentChannels() const {
    const int value = word(entry_);
    return {(value >> 1) & 7, (value >> 5) & 7, (value >> 9) & 7};
}

bool PalettesModel::setChannel(int channel, int level, QString* error) {
    if (channel < 0 || channel > 2 || level < 0 || level > 7) return false;
    const int entry = colourAt(entry_);
    if (entry < 0) {
        if (error) *error = tr("This command does not write that colour.");
        return false;
    }
    const int shift = 1 + channel * 4;
    const int row = command_;
    return session_->edit(
        Doc::Palettes, tr("Change colour"),
        [&](QJsonObject& palettes) {
            QJsonArray blocks = palettes.value(QStringLiteral("blocks")).toArray();
            const int at = indexOfLabel(blocks, label_);
            QJsonObject block = blocks.at(at).toObject();
            QJsonArray commands = block.value(QStringLiteral("commands")).toArray();
            QJsonObject command = commands.at(row).toObject();
            QJsonArray colours = command.value(QStringLiteral("colours")).toArray();
            const int value = colours.at(entry).toInt();
            colours.replace(entry, (value & ~(7 << shift)) | (level << shift));
            command.insert(QStringLiteral("colours"), colours);
            commands.replace(row, command);
            block.insert(QStringLiteral("commands"), commands);
            blocks.replace(at, block);
            palettes.insert(QStringLiteral("blocks"), blocks);
        },
        error);
}

QVector<sdk::PaletteSample> PalettesModel::samples() const {
    QVector<sdk::PaletteSample> result;
    const QJsonObject palettes = session_->doc(Doc::Palettes);
    const QJsonArray scenes = session_->doc(Doc::Stages).value(QStringLiteral("scenes")).toArray();
    for (int stage = 0; stage < scenes.size() && result.isEmpty(); ++stage) {
        const QJsonObject scene = scenes.at(stage).toObject();
        if (!listPlays(palettes, scene.value(QStringLiteral("palette_list")).toString(), label_) &&
            scene.value(QStringLiteral("visual_palette")).toString() != label_) {
            continue;
        }
        const Scene& drawn = session_->stage(stage);
        if (!drawn.planes[0].present && !drawn.planes[1].present) continue;
        sdk::PaletteSample sample;
        sample.label = session_->stageLabel(stage);
        sample.caption = tr("Stage start");
        sample.image = renderScene(drawn, QRect(0, 0, 320, 224));
        result.append(sample);
    }
    const QJsonArray screens = session_->doc(Doc::Screens).value(QStringLiteral("screens")).toArray();
    for (int index = 0; index < screens.size(); ++index) {
        const QJsonObject palette = screens.at(index).toObject().value(QStringLiteral("palette")).toObject();
        const QString list = palette.value(QStringLiteral("list")).toString();
        if (palette.value(QStringLiteral("command")).toString() != label_ &&
            (list.isEmpty() || !listPlays(palettes, list, label_))) {
            continue;
        }
        sdk::PaletteSample sample;
        sample.label = ScreensModel::screenTitle(screens.at(index).toObject().value(QStringLiteral("id")).toString());
        sample.caption = tr("Screen");
        sample.image = renderScene(session_->screen(index), QRect(0, 0, 320, 224));
        result.append(sample);
    }
    return result;
}

QString PalettesModel::statusText() const {
    return tr("%1 · line %2, entry %3").arg(title()).arg(entry_.y()).arg(entry_.x());
}

sdk::Inspection PalettesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Palette commands");
    inspection.title = title();
    sdk::Field value;
    value.key = QStringLiteral("word");
    value.label = tr("CRAM word");
    value.value = colourAt(entry_) < 0 ? QStringLiteral("—")
                                       : QStringLiteral("$%1").arg(word(entry_), 4, 16, QLatin1Char('0')).toUpper();
    value.monospace = true;
    inspection.fields = {value};
    inspection.note =
        tr("A command writes its colours from its destination on; which screens and stages play it is "
           "fixed, and so is the number of colours.");
    return inspection;
}

}  // namespace rl::alien_soldier
