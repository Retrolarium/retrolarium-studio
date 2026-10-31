#include "alien_soldier/plugin/palettes_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/bosses_model.hpp"
#include "alien_soldier/plugin/objects_model.hpp"
#include "alien_soldier/plugin/screens_model.hpp"
#include "alien_soldier/plugin/session.hpp"

#include "sdk/game_names.hpp"

#include <QJsonArray>
#include <QPainter>
#include <QSet>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

// At most this many objects show what a command no place plays colours.
constexpr int kObjectSamples = 6;

QJsonArray commandsOf(const QJsonObject& palettes, const QString& block) {
    const QJsonArray blocks = palettes.value(QStringLiteral("blocks")).toArray();
    const int at = indexOfLabel(blocks, block);
    return at < 0 ? QJsonArray() : blocks.at(at).toObject().value(QStringLiteral("commands")).toArray();
}

// The command of a block that starts `offset` bytes into it (-1: none).
int commandAt(const QJsonObject& palettes, const QString& block, int offset) {
    const QJsonArray commands = commandsOf(palettes, block);
    for (int index = 0; index < commands.size(); ++index) {
        if (commands.at(index).toObject().value(QStringLiteral("offset")).toInt() == offset) return index;
    }
    return -1;
}

QString stepKey(const PaletteStep& step) { return QStringLiteral("%1#%2").arg(step.block).arg(step.command + 1); }

QString stepTitle(const QJsonObject& palettes, const PaletteStep& step) {
    return commandsOf(palettes, step.block).size() > 1 ? QStringLiteral("%1 · %2").arg(step.block).arg(step.command + 1)
                                                       : step.block;
}

// [block, offset] pairs (a palette list, a boss's colours) as steps.
QVector<PaletteStep> stepsOf(const QJsonObject& palettes, const QJsonArray& pairs) {
    QVector<PaletteStep> steps;
    for (const QJsonValue& value : pairs) {
        const QJsonArray pair = value.toArray();
        const int command = commandAt(palettes, pair.at(0).toString(), pair.at(1).toInt());
        if (command >= 0) steps.append({pair.at(0).toString(), command});
    }
    return steps;
}

// The common command every list starts with, then the list's commands.
QVector<PaletteStep> listSteps(const QJsonObject& palettes, const QString& list) {
    QVector<PaletteStep> steps;
    const QString prelude = palettes.value(QStringLiteral("prelude")).toString();
    if (!commandsOf(palettes, prelude).isEmpty()) steps.append({prelude, 0});
    for (const QJsonValue& value : palettes.value(QStringLiteral("lists")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("label")).toString() == list)
            steps += stepsOf(palettes, item.value(QStringLiteral("commands")).toArray());
    }
    return steps;
}

// The lines of colour RAM a command writes, from its destination on.
QVector<int> linesOf(const QJsonObject& command) {
    const int start = command.value(QStringLiteral("destination")).toInt() / 2;
    const int count = int(command.value(QStringLiteral("colours")).toArray().size());
    QVector<int> lines;
    for (int line = start / 16; count > 0 && line <= std::min(3, (start + count - 1) / 16); ++line) lines << line;
    return lines;
}

bool plays(const QVector<PaletteStep>& steps, const PaletteStep& step) {
    return std::any_of(steps.begin(), steps.end(), [&](const PaletteStep& other) {
        return other.block == step.block && other.command == step.command;
    });
}

QString stagesTitle(const AlienSoldierSession* session, QVector<int> stages) {
    std::sort(stages.begin(), stages.end());
    stages.erase(std::unique(stages.begin(), stages.end()), stages.end());
    if (stages.size() == 1) return session->stageLabel(stages.first());
    if (stages.last() - stages.first() == stages.size() - 1)
        return PalettesModel::tr("Stages %1–%2").arg(stages.first() + 1).arg(stages.last() + 1);
    QStringList numbers;
    for (int stage : stages) numbers << QString::number(stage + 1);
    return PalettesModel::tr("Stages %1").arg(numbers.join(QStringLiteral(", ")));
}

// What a view draws, cut to its pixels, for the preview.
QImage cropped(const QImage& image, const QRect& focus) {
    const QRect area = focus.intersected(image.rect());
    return area.isEmpty() ? image : image.copy(area);
}

}  // namespace

QVector<PaletteLocation> PalettesModel::locations(const AlienSoldierSession* session) {
    const QJsonObject palettes = session->doc(Doc::Palettes);
    QVector<PaletteLocation> result;

    // Stages: the palette list a stage starts and the command its visual
    // loader plays, the stages that share both as one place.
    QStringList keys;
    QHash<QString, QVector<int>> stagesOf;
    const QJsonArray scenes = session->doc(Doc::Stages).value(QStringLiteral("scenes")).toArray();
    QSet<QString> stageLists;
    for (const QJsonValue& value : scenes) {
        const QJsonObject scene = value.toObject();
        const QString list = scene.value(QStringLiteral("palette_list")).toString();
        if (list.isEmpty()) continue;
        stageLists << list;
        const QString key =
            QStringLiteral("stages:%1:%2").arg(list, scene.value(QStringLiteral("visual_palette")).toString());
        if (!stagesOf.contains(key)) keys << key;
        stagesOf[key] << scene.value(QStringLiteral("index")).toInt();
    }
    for (const QString& key : keys) {
        PaletteLocation place;
        place.id = key;
        place.group = QStringLiteral("stages");
        place.title = stagesTitle(session, stagesOf.value(key));
        place.steps = listSteps(palettes, key.section(QLatin1Char(':'), 1, 1));
        const QString visual = key.section(QLatin1Char(':'), 2);
        if (!visual.isEmpty() && !commandsOf(palettes, visual).isEmpty()) place.steps.append({visual, 0});
        result.append(place);
    }

    // Screens.
    QSet<QString> screenLists;
    for (const QJsonValue& value : session->doc(Doc::Screens).value(QStringLiteral("screens")).toArray()) {
        const QJsonObject screen = value.toObject();
        const QJsonObject palette = screen.value(QStringLiteral("palette")).toObject();
        PaletteLocation place;
        place.id = QStringLiteral("screen:%1").arg(screen.value(QStringLiteral("id")).toString());
        place.group = QStringLiteral("screens");
        place.title = ScreensModel::screenTitle(screen.value(QStringLiteral("id")).toString());
        const QString list = palette.value(QStringLiteral("list")).toString();
        const QString command = palette.value(QStringLiteral("command")).toString();
        if (!list.isEmpty()) {
            screenLists << list;
            place.steps = listSteps(palettes, list);
        } else if (!commandsOf(palettes, command).isEmpty()) {
            place.steps = {{command, 0}};
        }
        if (!place.steps.isEmpty()) result.append(place);
    }

    // Boss fights: the stage's colours, then the boss's own command.
    for (const QJsonValue& value : session->doc(Doc::Bosses).value(QStringLiteral("bosses")).toArray()) {
        const QJsonObject boss = value.toObject();
        PaletteLocation place;
        place.id = QStringLiteral("boss:%1").arg(boss.value(QStringLiteral("id")).toString());
        place.group = QStringLiteral("bosses");
        place.title = bossPreviewTitle(boss);
        place.steps = stepsOf(palettes, boss.value(QStringLiteral("palettes")).toArray());
        if (!place.steps.isEmpty()) result.append(place);
    }

    // Lists no stage or screen names (the code starts them itself).
    for (const QJsonValue& value : palettes.value(QStringLiteral("lists")).toArray()) {
        const QString list = value.toObject().value(QStringLiteral("label")).toString();
        if (stageLists.contains(list) || screenLists.contains(list)) continue;
        result.append({QStringLiteral("list:%1").arg(list), QStringLiteral("lists"), list, listSteps(palettes, list)});
    }

    // Commands none of those play, each on its own.
    QSet<QString> played;
    for (const PaletteLocation& place : result)
        for (const PaletteStep& step : place.steps) played << stepKey(step);
    for (const QJsonValue& value : palettes.value(QStringLiteral("blocks")).toArray()) {
        const QString block = value.toObject().value(QStringLiteral("label")).toString();
        const int commands = int(value.toObject().value(QStringLiteral("commands")).toArray().size());
        for (int command = 0; command < commands; ++command) {
            const PaletteStep step{block, command};
            if (played.contains(stepKey(step))) continue;
            result.append({stepKey(step), QStringLiteral("commands"), stepTitle(palettes, step), {step}});
        }
    }
    return result;
}

int PalettesModel::lineCount(const QJsonObject& palettes, const QVector<PaletteStep>& steps) {
    int count = 0;
    for (const PaletteStep& step : steps)
        count += int(linesOf(commandsOf(palettes, step.block).at(step.command).toObject()).size());
    return count;
}

PalettesModel::PalettesModel(AlienSoldierSession* session, const QString& document)
    : PaletteModel(session), session_(session), document_(document) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Palettes) emit contentChanged();
    });
}

PaletteLocation PalettesModel::location() const {
    for (const PaletteLocation& place : locations(session_))
        if (place.id == document_) return place;
    // A command by "label#N" that a place plays, or a bare label (its first
    // command) from older views.
    const QJsonObject palettes = session_->doc(Doc::Palettes);
    const QString block = document_.section(QLatin1Char('#'), 0, 0);
    const int command = document_.contains(QLatin1Char('#')) ? document_.section(QLatin1Char('#'), 1).toInt() - 1 : 0;
    PaletteLocation place;
    place.id = document_;
    place.group = QStringLiteral("commands");
    if (command >= 0 && command < commandsOf(palettes, block).size()) {
        place.steps = {{block, command}};
        place.title = stepTitle(palettes, place.steps.first());
    }
    return place;
}

QVector<PalettesModel::Line> PalettesModel::lines() const {
    QVector<Line> result;
    const PaletteLocation place = location();
    for (int step = 0; step < place.steps.size(); ++step)
        for (int line : linesOf(command(place.steps.at(step)))) result.append({step, line});
    return result;
}

QJsonObject PalettesModel::command(const PaletteStep& step) const {
    return commandsOf(session_->doc(Doc::Palettes), step.block).at(step.command).toObject();
}

QString PalettesModel::title() const {
    const QString title = location().title;
    return title.isEmpty() ? document_ : title;
}

int PalettesModel::colourAt(QPoint entry) const {
    const QVector<Line> all = lines();
    if (entry.y() < 0 || entry.y() >= all.size() || entry.x() < 0 || entry.x() > 15) return -1;
    const Line line = all.at(entry.y());
    const QJsonObject item = command(location().steps.at(line.step));
    const int index = line.line * 16 + entry.x() - item.value(QStringLiteral("destination")).toInt() / 2;
    return index >= 0 && index < item.value(QStringLiteral("colours")).toArray().size() ? index : -1;
}

int PalettesModel::word(QPoint entry) const {
    const int index = colourAt(entry);
    if (index < 0) return 0;
    const PaletteStep step = location().steps.at(lines().at(entry.y()).step);
    return command(step).value(QStringLiteral("colours")).toArray().at(index).toInt();
}

QVector<sdk::PaletteRow> PalettesModel::rows() const {
    QVector<sdk::PaletteRow> result;
    const QJsonObject palettes = session_->doc(Doc::Palettes);
    const PaletteLocation place = location();
    const QVector<Line> all = lines();
    for (int row = 0; row < all.size(); ++row) {
        sdk::PaletteRow item;
        item.group = stepTitle(palettes, place.steps.at(all.at(row).step));
        item.label = tr("Line %1").arg(all.at(row).line);
        for (int entry = 0; entry < 16; ++entry) {
            if (colourAt(QPoint(entry, row)) < 0) {
                // Not this command's: CRAM keeps what others wrote there.
                item.colors.append(QColor(0x1c, 0x1e, 0x24));
                item.labels << QString();
                continue;
            }
            const int value = word(QPoint(entry, row));
            item.colors.append(QColor::fromRgb(cramColor(value)));
            item.labels << QStringLiteral("%1").arg(value, 3, 16, QLatin1Char('0')).toUpper();
        }
        result.append(item);
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
    const PaletteStep step = location().steps.at(lines().at(entry_.y()).step);
    const int shift = 1 + channel * 4;
    return session_->edit(
        Doc::Palettes, tr("Change colour"),
        [&](QJsonObject& palettes) {
            QJsonArray blocks = palettes.value(QStringLiteral("blocks")).toArray();
            const int at = indexOfLabel(blocks, step.block);
            QJsonObject block = blocks.at(at).toObject();
            QJsonArray commands = block.value(QStringLiteral("commands")).toArray();
            QJsonObject command = commands.at(step.command).toObject();
            QJsonArray colours = command.value(QStringLiteral("colours")).toArray();
            const int value = colours.at(entry).toInt();
            colours.replace(entry, (value & ~(7 << shift)) | (level << shift));
            command.insert(QStringLiteral("colours"), colours);
            commands.replace(step.command, command);
            block.insert(QStringLiteral("commands"), commands);
            blocks.replace(at, block);
            palettes.insert(QStringLiteral("blocks"), blocks);
        },
        error);
}

QVector<sdk::PaletteSample> PalettesModel::samples() const {
    QVector<sdk::PaletteSample> result;
    const QJsonObject palettes = session_->doc(Doc::Palettes);
    const PaletteLocation place = location();
    const QJsonArray scenes = session_->doc(Doc::Stages).value(QStringLiteral("scenes")).toArray();
    const QJsonArray screens = session_->doc(Doc::Screens).value(QStringLiteral("screens")).toArray();
    const auto addStage = [&](int index) {
        const Scene& drawn = session_->stage(index);
        if (!drawn.planes[0].present && !drawn.planes[1].present) return;
        sdk::PaletteSample sample;
        sample.label = session_->stageLabel(scenes.at(index).toObject().value(QStringLiteral("index")).toInt());
        sample.caption = tr("Stage start");
        sample.image = renderScene(drawn, QRect(0, 0, 320, 224));
        result.append(sample);
    };
    const auto addScreen = [&](int index) {
        sdk::PaletteSample sample;
        sample.label = ScreensModel::screenTitle(screens.at(index).toObject().value(QStringLiteral("id")).toString());
        sample.caption = tr("Screen");
        sample.image = renderScene(session_->screen(index), QRect(0, 0, 320, 224));
        result.append(sample);
    };
    const auto addBoss = [&](const QJsonObject& boss) {
        sdk::PaletteSample sample;
        sample.label = bossPreviewTitle(boss);
        sample.caption = tr("Boss");
        sample.image = makeBossPreview(session_, boss, true).image;
        sample.tightFrame = true;
        result.append(sample);
    };

    if (place.group == QLatin1String("stages")) {
        for (int index = 0; index < scenes.size(); ++index) {
            const QJsonObject scene = scenes.at(index).toObject();
            const QString key = QStringLiteral("stages:%1:%2")
                                    .arg(scene.value(QStringLiteral("palette_list")).toString(),
                                         scene.value(QStringLiteral("visual_palette")).toString());
            if (key == place.id) addStage(index);
        }
        return result;
    }
    if (place.group == QLatin1String("screens")) {
        const QString id = place.id.mid(7);
        for (int index = 0; index < screens.size(); ++index)
            if (screens.at(index).toObject().value(QStringLiteral("id")).toString() == id) addScreen(index);
        return result;
    }
    const QJsonArray bosses = session_->doc(Doc::Bosses).value(QStringLiteral("bosses")).toArray();
    if (place.group == QLatin1String("bosses")) {
        for (const QJsonValue& value : bosses)
            if (QStringLiteral("boss:%1").arg(value.toObject().value(QStringLiteral("id")).toString()) == place.id)
                addBoss(value.toObject());
        return result;
    }

    // A list or a command the code plays itself: the bosses and objects
    // that draw with its commands, and stages whose loader plays it.
    const QVector<PaletteStep> own = place.group == QLatin1String("lists") && place.steps.size() > 1
                                         ? QVector<PaletteStep>(place.steps.begin() + 1, place.steps.end())
                                         : place.steps;
    for (int index = 0; index < scenes.size(); ++index) {
        const PaletteStep visual{scenes.at(index).toObject().value(QStringLiteral("visual_palette")).toString(), 0};
        if (plays(own, visual)) addStage(index);
    }
    for (const QJsonValue& value : bosses) {
        const QJsonObject boss = value.toObject();
        const QVector<PaletteStep> steps = stepsOf(palettes, boss.value(QStringLiteral("palettes")).toArray());
        if (std::any_of(own.begin(), own.end(), [&](const PaletteStep& step) { return plays(steps, step); }))
            addBoss(boss);
    }
    const QJsonObject contexts = session_->doc(Doc::Objects).value(QStringLiteral("contexts")).toObject();
    int objects = 0;
    for (auto it = contexts.begin(); it != contexts.end() && objects < kObjectSamples; ++it) {
        const QVector<PaletteStep> steps =
            stepsOf(palettes, it.value().toObject().value(QStringLiteral("palettes")).toArray());
        if (!std::any_of(own.begin(), own.end(), [&](const PaletteStep& step) { return plays(steps, step); })) continue;
        ObjectsModel model(session_, it.key());
        sdk::PaletteSample sample;
        sample.label = it.key();
        sample.caption = tr("Object");
        sample.image = cropped(model.render(), model.focusRect());
        result.append(sample);
        ++objects;
    }
    if (!result.isEmpty()) return result;

    // Nothing the studio draws uses these colours: the lines themselves.
    const QVector<sdk::PaletteRow> lines = rows();
    QImage strip(16 * 12, std::max(1, int(lines.size())) * 12, QImage::Format_RGB32);
    strip.fill(QColor(0x1c, 0x1e, 0x24));
    QPainter painter(&strip);
    for (int row = 0; row < lines.size(); ++row)
        for (int entry = 0; entry < lines.at(row).colors.size(); ++entry)
            painter.fillRect(entry * 12, row * 12, 12, 12, lines.at(row).colors.at(entry));
    painter.end();
    sdk::PaletteSample sample;
    sample.label = title();
    sample.caption = tr("No view draws with these colours yet; the code plays them itself.");
    sample.image = strip;
    result.append(sample);
    return result;
}

QString PalettesModel::statusText() const {
    const QVector<Line> all = lines();
    const int line = entry_.y() >= 0 && entry_.y() < all.size() ? all.at(entry_.y()).line : 0;
    return tr("%1 · line %2, entry %3").arg(title()).arg(line).arg(entry_.x());
}

sdk::Inspection PalettesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Palette commands");
    inspection.title = title();
    const QVector<Line> all = lines();
    if (entry_.y() >= 0 && entry_.y() < all.size()) {
        sdk::Field command;
        command.key = QStringLiteral("command");
        command.label = tr("Command");
        command.value = stepTitle(session_->doc(Doc::Palettes), location().steps.at(all.at(entry_.y()).step));
        command.monospace = true;
        inspection.fields.append(command);
    }
    sdk::Field value;
    value.key = QStringLiteral("word");
    value.label = tr("CRAM word");
    value.value = colourAt(entry_) < 0 ? QStringLiteral("—")
                                       : QStringLiteral("$%1").arg(word(entry_), 4, 16, QLatin1Char('0')).toUpper();
    value.monospace = true;
    inspection.fields.append(value);
    inspection.note =
        tr("A place's colours are the commands it plays, in order, each writing its colours from its destination "
           "on; only the lines they write are shown. A command several places play changes in all of them. Which "
           "places play it is fixed, and so is the number of colours.");
    return inspection;
}

}  // namespace rl::alien_soldier
