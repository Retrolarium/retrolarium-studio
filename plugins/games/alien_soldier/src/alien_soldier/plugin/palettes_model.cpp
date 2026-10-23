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

PalettesModel::PalettesModel(AlienSoldierSession* session, const QString& label)
    : PaletteModel(session), session_(session), label_(label) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Palettes) emit contentChanged();
    });
}

int PalettesModel::word(int row, int entry) const {
    return commandsOf(session_->doc(Doc::Palettes), label_)
        .at(row)
        .toObject()
        .value(QStringLiteral("colours"))
        .toArray()
        .at(entry)
        .toInt();
}

QVector<sdk::PaletteRow> PalettesModel::rows() const {
    QVector<sdk::PaletteRow> result;
    for (const QJsonValue& value : commandsOf(session_->doc(Doc::Palettes), label_)) {
        const QJsonObject command = value.toObject();
        sdk::PaletteRow row;
        const int first = command.value(QStringLiteral("destination")).toInt() / 2;
        row.label = tr("Line %1 · colour %2").arg(first / 16).arg(first % 16);
        for (const QJsonValue& colour : command.value(QStringLiteral("colours")).toArray()) {
            row.colors.append(QColor::fromRgb(cramColor(colour.toInt())));
            row.labels << QStringLiteral("%1").arg(colour.toInt(), 3, 16, QLatin1Char('0')).toUpper();
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
    const int value = word(entry_.y(), entry_.x());
    return {(value >> 1) & 7, (value >> 5) & 7, (value >> 9) & 7};
}

bool PalettesModel::setChannel(int channel, int level, QString* error) {
    if (channel < 0 || channel > 2 || level < 0 || level > 7) return false;
    const int shift = 1 + channel * 4;
    const int row = entry_.y(), entry = entry_.x();
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
    return tr("%1 · line %2, entry %3").arg(label_).arg(entry_.y()).arg(entry_.x());
}

sdk::Inspection PalettesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Palette commands");
    inspection.title = label_;
    sdk::Field value;
    value.key = QStringLiteral("word");
    value.label = tr("CRAM word");
    value.value = QStringLiteral("$%1").arg(word(entry_.y(), entry_.x()), 4, 16, QLatin1Char('0')).toUpper();
    value.monospace = true;
    inspection.fields = {value};
    inspection.note =
        tr("A command writes its colours from its destination on; which screens and stages play it is "
           "fixed, and so is the number of colours.");
    return inspection;
}

}  // namespace rl::alien_soldier
