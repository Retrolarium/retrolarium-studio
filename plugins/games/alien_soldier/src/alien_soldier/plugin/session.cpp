#include "alien_soldier/plugin/session.hpp"

#include "alien_soldier/plugin/animations_model.hpp"
#include "alien_soldier/plugin/bosses_model.hpp"
#include "alien_soldier/plugin/demo_model.hpp"
#include "alien_soldier/plugin/levels_model.hpp"
#include "alien_soldier/plugin/metatiles_model.hpp"
#include "alien_soldier/plugin/object_metatiles_model.hpp"
#include "alien_soldier/plugin/objects_model.hpp"
#include "alien_soldier/plugin/palettes_model.hpp"
#include "alien_soldier/plugin/playtest_model.hpp"
#include "alien_soldier/plugin/screens_model.hpp"
#include "alien_soldier/plugin/sound_model.hpp"
#include "alien_soldier/plugin/text_model.hpp"
#include "alien_soldier/plugin/tiles_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoStack>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

const QString kSnapshot = QStringLiteral("alien-soldier-source");

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta, bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

sdk::Kind kind(const char* id, const QString& label, sdk::EditorType editor, const char* icon = nullptr) {
    sdk::Kind result;
    result.id = QString::fromLatin1(id);
    result.label = label;
    result.icon = QString::fromLatin1(icon ? icon : id);
    result.editor = editor;
    return result;
}

QString two(int value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

// Which documents a view's document list marks dirty by.
bool payloadDirty(const QJsonObject& now, const QJsonObject& saved, const QString& list, const QString& label) {
    const QJsonArray a = now.value(list).toArray(), b = saved.value(list).toArray();
    const int index = indexOfLabel(a, label);
    return index >= 0 && a.at(index) != b.at(index);
}

}  // namespace

// ---------------------------------------------------------------- opening

std::unique_ptr<AlienSoldierSession> AlienSoldierSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<AlienSoldierSession> session(new AlienSoldierSession);
    session->project_ = project;
    try {
        session->snapshot_ = project.resolve(kSnapshot);
        session->documents_ = Documents::load(session->snapshot_);
        const QJsonObject record =
            loadJson(QDir(session->snapshot_).filePath(QStringLiteral("retrolarium-source-snapshot.json")));
        session->revision_ = record.value(QStringLiteral("revision")).toString();
    } catch (const std::exception& e) {
        if (error) *error = tr("Cannot open the Alien Soldier project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile AlienSoldierSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Alien Soldier");
    profile.badge = QStringLiteral("MD");
    profile.platform = tr("Mega Drive");
    profile.revision = revision_.toUpper();
    profile.cpu = QStringLiteral("68000");
    profile.chip = tr("YM2612 + PSG");
    profile.codec = QStringLiteral("source");
    return profile;
}

QVector<sdk::Kind> AlienSoldierSession::kinds() const {
    return {
        kind("levels", tr("Levels"), sdk::EditorType::TileMap),
        kind("tiles", tr("Tiles"), sdk::EditorType::Tile),
        kind("metatiles", tr("Metatiles"), sdk::EditorType::TileMap, "tileset"),
        kind("palettes", tr("Palettes"), sdk::EditorType::Palette),
        kind("objects", tr("Objects"), sdk::EditorType::Composition, "sprites"),
        kind("bosses", tr("Bosses"), sdk::EditorType::Composition),
        kind("sprites", tr("Animations"), sdk::EditorType::Composition),
        kind("screens", tr("Screens"), sdk::EditorType::TileMap, "tileset"),
        kind("text", tr("Text"), sdk::EditorType::Table),
        kind("sound", tr("Sound"), sdk::EditorType::Sequencer),
        kind("demo", tr("Demo"), sdk::EditorType::Table, "play"),
        kind("playtest", tr("Playtest"), sdk::EditorType::Emulator),
    };
}

int AlienSoldierSession::stageCount() const {
    return doc(Doc::Stages).value(QStringLiteral("scenes")).toArray().size();
}

QString AlienSoldierSession::stageLabel(int index) const {
    const QString name = sdk::gameName("stages", index, QString());
    return name.isEmpty() ? tr("Stage slot %1").arg(two(index + 1)) : tr("Stage %1 · %2").arg(two(index + 1), name);
}

QVector<BlockSet> AlienSoldierSession::blockSets() const {
    QVector<BlockSet> sets;
    for (const QJsonValue& value : doc(Doc::Stages).value(QStringLiteral("block_sets")).toArray()) {
        const QJsonObject item = value.toObject();
        BlockSet set;
        set.id = item.value(QStringLiteral("id")).toString();
        set.plane = item.value(QStringLiteral("plane")).toInt();
        for (const QJsonValue& stage : item.value(QStringLiteral("stages")).toArray()) set.stages << stage.toInt();
        if (!set.stages.isEmpty()) sets << set;
    }
    return sets;
}

int AlienSoldierSession::blockSetOf(int stage) const {
    const QVector<BlockSet> sets = blockSets();
    for (int index = 0; index < sets.size(); ++index) {
        if (sets[index].stages.contains(stage)) return index;
    }
    return -1;
}

QString AlienSoldierSession::blockSetLabel(int set) const {
    const QVector<BlockSet> sets = blockSets();
    if (set < 0 || set >= sets.size()) return {};
    const QVector<int>& stages = sets[set].stages;
    bool runs = true;
    for (int index = 1; index < stages.size(); ++index) runs = runs && stages[index] == stages[index - 1] + 1;
    if (stages.size() == 1) return tr("Stage %1").arg(two(stages.first() + 1));
    if (runs) return tr("Stages %1–%2").arg(two(stages.first() + 1), two(stages.last() + 1));
    QStringList numbers;
    for (int stage : stages) numbers << two(stage + 1);
    return tr("Stages %1").arg(numbers.join(QStringLiteral(", ")));
}

QRect AlienSoldierSession::stageArea(int stage) const {
    const QJsonObject area = doc(Doc::Stages)
                                 .value(QStringLiteral("scenes"))
                                 .toArray()
                                 .at(stage)
                                 .toObject()
                                 .value(QStringLiteral("area"))
                                 .toObject();
    if (area.isEmpty()) return {};
    return QRect(area.value(QStringLiteral("x")).toInt(), area.value(QStringLiteral("y")).toInt(),
                 area.value(QStringLiteral("width")).toInt(), area.value(QStringLiteral("height")).toInt());
}

QVector<QRect> AlienSoldierSession::stageParts(int stage) const {
    const QJsonArray parts = doc(Doc::Stages)
                                 .value(QStringLiteral("scenes"))
                                 .toArray()
                                 .at(stage)
                                 .toObject()
                                 .value(QStringLiteral("area"))
                                 .toObject()
                                 .value(QStringLiteral("parts"))
                                 .toArray();
    if (parts.isEmpty()) {
        const QRect area = stageArea(stage);
        return area.isEmpty() ? QVector<QRect>() : QVector<QRect>{area};
    }
    QVector<QRect> found;
    for (const QJsonValue& value : parts) {
        const QJsonObject part = value.toObject();
        found.append(QRect(part.value(QStringLiteral("x")).toInt(), part.value(QStringLiteral("y")).toInt(),
                           part.value(QStringLiteral("width")).toInt(), part.value(QStringLiteral("height")).toInt()));
    }
    return found;
}

QString AlienSoldierSession::stagePartLabel(int stage, int part) const {
    const int arenas = int(stageArenas(stage).size());
    const QString name = sdk::gameName("stages", stage, QString());
    const QStringList names = name.split(QStringLiteral(" / "));
    if (arenas > 0 && names.size() == arenas + 1 && part >= 0 && part < names.size())
        return tr("Stage %1 · %2").arg(two(stage + 1), names.at(part).trimmed());
    // One name and one arena: the boss is fought there (the airborne boss), the
    // stage itself is the run up to it.
    if (arenas == 1 && names.size() == 1 && !name.isEmpty())
        return part == 0 ? tr("Stage %1").arg(two(stage + 1)) : stageLabel(stage);
    return part == 0 ? stageLabel(stage) : tr("%1 · arena").arg(stageLabel(stage));
}

bool AlienSoldierSession::stageApart(int stage) const {
    return doc(Doc::Stages)
        .value(QStringLiteral("scenes"))
        .toArray()
        .at(stage)
        .toObject()
        .value(QStringLiteral("area"))
        .toObject()
        .value(QStringLiteral("apart"))
        .toBool();
}

QVector<QRect> AlienSoldierSession::stageArenas(int stage) const {
    QVector<QRect> found;
    const QJsonArray arenas = doc(Doc::Stages)
                                  .value(QStringLiteral("scenes"))
                                  .toArray()
                                  .at(stage)
                                  .toObject()
                                  .value(QStringLiteral("arenas"))
                                  .toArray();
    for (const QJsonValue& value : arenas) {
        const QJsonObject arena = value.toObject();
        if (arena.value(QStringLiteral("inline")).toBool()) continue;  // shown with the stage
        found.append(QRect(arena.value(QStringLiteral("x")).toInt(), arena.value(QStringLiteral("y")).toInt(),
                           arena.value(QStringLiteral("width")).toInt(),
                           arena.value(QStringLiteral("height")).toInt()));
    }
    return found;
}

QPoint AlienSoldierSession::stageStart(int stage) const {
    const QJsonObject scene = doc(Doc::Stages).value(QStringLiteral("scenes")).toArray().at(stage).toObject();
    const QString label = scene.value(QStringLiteral("record")).toString();
    const QJsonArray records = doc(Doc::Levels).value(QStringLiteral("records")).toArray();
    const QJsonObject record = records.at(indexOfLabel(records, label)).toObject();
    // The stage's handlers may move the camera off the record's X at once.
    const QJsonObject area = scene.value(QStringLiteral("area")).toObject();
    const int x = area.contains(QStringLiteral("start")) ? area.value(QStringLiteral("start")).toInt()
                                                         : record.value(QStringLiteral("camera_x")).toInt();
    const int y = record.value(QStringLiteral("camera_y")).toInt();
    // The plane streamer reads the coarse map at row -Y.
    return {x, (0x10000 - y) & 0x1FFF};
}

// ---------------------------------------------------------------- documents

QVector<sdk::DocumentGroup> AlienSoldierSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    const auto group = [](const QString& title) {
        sdk::DocumentGroup result;
        result.title = title;
        return result;
    };
    if (kind == QLatin1String("levels")) {
        // A level per block set: the whole layout, then each stage's part.
        const bool dirty = documents_.tracked(Doc::Levels).isDirty() || documents_.tracked(Doc::Maps).isDirty();
        const QVector<BlockSet> sets = blockSets();
        for (int set = 0; set < sets.size(); ++set) {
            sdk::DocumentGroup level = group(blockSetLabel(set));
            level.documents.append(entry(QStringLiteral("set-%1").arg(set + 1), tr("Whole level"), {}, dirty));
            for (int stage : sets[set].stages) {
                level.documents.append(
                    entry(QStringLiteral("stage-") + two(stage + 1), stagePartLabel(stage, 0), {}, dirty));
                const int arenas = int(stageArenas(stage).size());
                for (int arena = 0; arena < arenas; ++arena) {
                    level.documents.append(entry(QStringLiteral("arena-%1-%2").arg(two(stage + 1)).arg(arena + 1),
                                                 stagePartLabel(stage, arena + 1), {}, dirty));
                }
            }
            groups.append(level);
        }
        sdk::DocumentGroup other = group(tr("Without a map"));
        for (int stage = 0; stage < stageCount(); ++stage) {
            if (blockSetOf(stage) < 0)
                other.documents.append(
                    entry(QStringLiteral("stage-") + two(stage + 1), stageLabel(stage), tr("no map"), dirty));
        }
        if (!other.documents.isEmpty()) groups.append(other);
    } else if (kind == QLatin1String("metatiles")) {
        // The objects' metatiles by file, then the chunks and patterns of
        // each block set, plane B's beside plane A's.
        sdk::DocumentGroup objects = group(tr("Objects"));
        const bool objectsDirty = documents_.tracked(Doc::Mappings).isDirty();
        for (const QJsonValue& value : doc(Doc::Objects).value(QStringLiteral("groups")).toArray()) {
            const QJsonObject item = value.toObject();
            QStringList blocks;
            for (const QJsonValue& label : item.value(QStringLiteral("blocks")).toArray()) blocks << label.toString();
            const int count = int(metatilesOf(documents_, blocks).size());
            if (count == 0) continue;
            const QString id = item.value(QStringLiteral("id")).toString();
            objects.documents.append(entry(QStringLiteral("obj:") + id,
                                           item.value(QStringLiteral("title")).toString(id),
                                           tr("%n metatiles", nullptr, count), objectsDirty));
        }
        if (!objects.documents.isEmpty()) groups.append(objects);
        sdk::DocumentGroup levels = group(tr("Levels"));
        const bool dirty = documents_.tracked(Doc::Maps).isDirty();
        const QVector<BlockSet> sets = blockSets();
        for (int set = 0; set < sets.size(); ++set) {
            const Scene& scene = stage(sets[set].stages.first());
            for (int plane = 0; plane < 2; ++plane) {
                const int chunks = chunkCount(scene, plane);
                if (chunks == 0) continue;
                levels.documents.append(
                    entry(QStringLiteral("set-%1-%2").arg(set + 1).arg(QLatin1Char(plane ? 'b' : 'a')),
                          tr("%1 · %2").arg(blockSetLabel(set), plane ? tr("Plane B") : tr("Plane A")),
                          tr("%n chunks", nullptr, chunks), dirty));
            }
        }
        if (!levels.documents.isEmpty()) groups.append(levels);
    } else if (kind == QLatin1String("tiles")) {
        // The art by what draws with it (art.json); the player's pieces are
        // one sprite sheet, and screen maps belong to the screens.
        QMap<QString, sdk::DocumentGroup> byGroup;
        QStringList order;
        const auto add = [&](const QString& key, const QString& title, const sdk::DocumentEntry& item) {
            if (!byGroup.contains(key)) {
                byGroup.insert(key, group(title));
                order << key;
            }
            byGroup[key].documents.append(item);
        };
        const QJsonArray compressed = doc(Doc::Compressed).value(QStringLiteral("payloads")).toArray();
        const QJsonArray plain = doc(Doc::Tiles).value(QStringLiteral("payloads")).toArray();
        QJsonArray art = doc(Doc::Art).value(QStringLiteral("payloads")).toArray();
        if (art.isEmpty()) {
            // A project the kit has not read again yet: every payload, as it comes.
            for (const QJsonValue& value : compressed) {
                QJsonObject item;
                item.insert(QStringLiteral("label"), value.toObject().value(QStringLiteral("label")));
                item.insert(QStringLiteral("group"), QStringLiteral("other"));
                art.append(item);
            }
            for (const QJsonValue& value : plain) {
                QJsonObject item;
                item.insert(QStringLiteral("label"), value.toObject().value(QStringLiteral("label")));
                item.insert(QStringLiteral("group"), QStringLiteral("other"));
                art.append(item);
            }
        }
        for (const QJsonValue& value : art) {
            const QJsonObject item = value.toObject();
            const QString label = item.value(QStringLiteral("label")).toString();
            const QString kind = item.value(QStringLiteral("group")).toString();
            if (kind == QLatin1String("map")) continue;
            const bool isCompressed = indexOfLabel(compressed, label) >= 0;
            const QJsonObject payload = isCompressed ? compressed.at(indexOfLabel(compressed, label)).toObject()
                                                     : plain.at(indexOfLabel(plain, label)).toObject();
            int count = payload.value(QStringLiteral("tiles")).toArray().size();
            for (const QJsonValue& block : payload.value(QStringLiteral("blocks")).toArray())
                count += block.toObject().value(QStringLiteral("tiles")).toArray().size();
            const Doc owner = isCompressed ? Doc::Compressed : Doc::Tiles;
            const sdk::DocumentEntry document = entry(
                (isCompressed ? QStringLiteral("c:") : QStringLiteral("u:")) + label, label, QString::number(count),
                payloadDirty(doc(owner), savedDoc(owner), QStringLiteral("payloads"), label));
            if (kind == QLatin1String("level")) {
                const int set = item.value(QStringLiteral("set")).toInt(-1);
                add(QStringLiteral("level-%1").arg(set), set >= 0 ? blockSetLabel(set) : tr("Stages"), document);
            } else {
                // The hero's art as tiles too; its frames are in Objects.
                const QString title = kind == QLatin1String("hero")      ? tr("Hero")
                                      : kind == QLatin1String("weapons") ? tr("Weapons")
                                      : kind == QLatin1String("objects") ? tr("Objects")
                                      : kind == QLatin1String("bosses")  ? tr("Bosses")
                                      : kind == QLatin1String("screen")  ? tr("Screens and fonts")
                                      : kind == QLatin1String("credits") ? tr("Credits")
                                                                         : tr("Other");
                add(kind, title, document);
            }
        }
        // The hero first, then objects, the levels in order, the rest.
        QStringList sorted;
        for (const char* key : {"hero", "weapons", "objects", "bosses"})
            if (order.contains(QLatin1String(key))) sorted << QString::fromLatin1(key);
        for (const QString& key : order)
            if (key.startsWith(QLatin1String("level-"))) sorted << key;
        for (const char* key : {"screen", "credits", "other"})
            if (order.contains(QLatin1String(key))) sorted << QString::fromLatin1(key);
        for (const QString& key : sorted) groups << byGroup.value(key);
    } else if (kind == QLatin1String("palettes")) {
        // Each place the game shows colours, with the commands it plays.
        const QJsonObject palettes = doc(Doc::Palettes);
        const QJsonObject saved = savedDoc(Doc::Palettes);
        const QVector<std::pair<QString, QString>> kinds = {{QStringLiteral("stages"), tr("Stages")},
                                                            {QStringLiteral("screens"), tr("Screens")},
                                                            {QStringLiteral("bosses"), tr("Bosses")},
                                                            {QStringLiteral("lists"), tr("Other palette lists")},
                                                            {QStringLiteral("commands"), tr("Commands no list plays")}};
        const QVector<PaletteLocation> places = PalettesModel::locations(this);
        for (const auto& [key, title] : kinds) {
            sdk::DocumentGroup section = group(title);
            for (const PaletteLocation& place : places) {
                if (place.group != key) continue;
                bool dirty = false;
                for (const PaletteStep& step : place.steps)
                    dirty = dirty || payloadDirty(palettes, saved, QStringLiteral("blocks"), step.block);
                section.documents.append(entry(place.id, place.title,
                                               tr("%n lines", nullptr, PalettesModel::lineCount(palettes, place.steps)),
                                               dirty));
            }
            if (!section.documents.isEmpty()) groups.append(section);
        }
    } else if (kind == QLatin1String("objects")) {
        // The mapping blocks by the source file that holds them.
        const QJsonObject objects = doc(Doc::Objects);
        const QJsonObject contexts = objects.value(QStringLiteral("contexts")).toObject();
        const QJsonArray blocks = doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
        const bool dirty = documents_.tracked(Doc::Mappings).isDirty();
        for (const QJsonValue& value : objects.value(QStringLiteral("groups")).toArray()) {
            const QJsonObject item = value.toObject();
            // The kit's title for the file ("the extended-reach boss"), else its name.
            sdk::DocumentGroup file =
                group(item.value(QStringLiteral("title")).toString(item.value(QStringLiteral("id")).toString()));
            for (const QJsonValue& label : item.value(QStringLiteral("blocks")).toArray()) {
                const QJsonObject block = blocks.at(indexOfLabel(blocks, label.toString())).toObject();
                const int count = block.value(QStringLiteral("mappings")).toArray().size();
                const QString meta = contexts.contains(label.toString()) ? tr("%n frames", nullptr, count)
                                                                         : tr("%n frames · not seen", nullptr, count);
                file.documents.append(entry(label.toString(), label.toString(), meta, dirty));
            }
            groups.append(file);
        }
        // The bosses their code alone turns, each as its setup leaves it.
        sdk::DocumentGroup coded = group(tr("Bosses (code)"));
        for (const QJsonValue& value : doc(Doc::Bosses).value(QStringLiteral("bosses")).toArray()) {
            const QJsonObject boss = value.toObject();
            if (!boss.value(QStringLiteral("poses")).isNull()) continue;
            coded.documents.append(entry(QStringLiteral("boss:") + boss.value(QStringLiteral("id")).toString(),
                                         boss.value(QStringLiteral("title")).toString(),
                                         BossesModel::tr("Set by its code")));
        }
        if (!coded.documents.isEmpty()) groups.append(coded);
    } else if (kind == QLatin1String("bosses")) {
        // Each boss with a pose table, with its animations.
        const QJsonObject document = doc(Doc::Bosses);
        const QJsonArray sequences = document.value(QStringLiteral("sequences")).toArray();
        const bool dirty = documents_.tracked(Doc::Bosses).isDirty();
        for (const QJsonValue& value : document.value(QStringLiteral("bosses")).toArray()) {
            const QJsonObject boss = value.toObject();
            if (boss.value(QStringLiteral("poses")).isNull()) continue;
            const QString id = boss.value(QStringLiteral("id")).toString();
            const QString name = boss.value(QStringLiteral("title")).toString();
            sdk::DocumentGroup item = group(name);
            for (const QJsonValue& label : boss.value(QStringLiteral("sequences")).toArray()) {
                const QJsonArray commands = sequences.at(indexOfLabel(sequences, label.toString()))
                                                .toObject()
                                                .value(QStringLiteral("commands"))
                                                .toArray();
                const int keys = int(std::count_if(commands.begin(), commands.end(), [](const QJsonValue& command) {
                    return command.toObject().contains(QStringLiteral("pose"));
                }));
                item.documents.append(entry(QStringLiteral("anim:%1:%2").arg(id, label.toString()),
                                            BossesModel::animationTitle(label.toString(), name),
                                            tr("%n keyframes", nullptr, keys), dirty));
            }
            groups.append(item);
        }
    } else if (kind == QLatin1String("sprites")) {
        sdk::DocumentGroup scripts = group(tr("Animations"));
        for (const QJsonValue& value : doc(Doc::Animations).value(QStringLiteral("scripts")).toArray()) {
            const QJsonObject script = value.toObject();
            const QString label = script.value(QStringLiteral("label")).toString();
            scripts.documents.append(
                entry(label, label, QString::number(script.value(QStringLiteral("entries")).toArray().size()),
                      payloadDirty(doc(Doc::Animations), savedDoc(Doc::Animations), QStringLiteral("scripts"), label) ||
                          documents_.tracked(Doc::Mappings).isDirty()));
        }
        groups.append(scripts);
    } else if (kind == QLatin1String("screens")) {
        // The game's screens, then the credits' picture scenes.
        sdk::DocumentGroup screens = group(tr("Screens"));
        sdk::DocumentGroup credits = group(tr("Credits"));
        const bool dirty = documents_.tracked(Doc::Screens).isDirty() || documents_.tracked(Doc::Compressed).isDirty();
        for (const QJsonValue& value : doc(Doc::Screens).value(QStringLiteral("screens")).toArray()) {
            const QJsonObject screen = value.toObject();
            const QString id = screen.value(QStringLiteral("id")).toString();
            const bool scene = screen.value(QStringLiteral("kind")).toString() == QLatin1String("credits");
            (scene ? credits : screens).documents.append(entry(id, ScreensModel::screenTitle(id), QString(), dirty));
        }
        groups.append(screens);
        if (!credits.documents.isEmpty()) groups.append(credits);
    } else if (kind == QLatin1String("text")) {
        QMap<QString, sdk::DocumentGroup> byGroup;
        QStringList order;
        for (const QJsonValue& value : doc(Doc::Text).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString name = payload.value(QStringLiteral("group")).toString();
            if (!byGroup.contains(name)) {
                byGroup.insert(name, group(TextModel::groupTitle(name)));
                order << name;
            }
            const QString label = payload.value(QStringLiteral("label")).toString();
            byGroup[name].documents.append(
                entry(label, label, QString::number(payload.value(QStringLiteral("records")).toArray().size()),
                      payloadDirty(doc(Doc::Text), savedDoc(Doc::Text), QStringLiteral("payloads"), label)));
        }
        for (const QString& name : order) groups.append(byGroup.value(name));
    } else if (kind == QLatin1String("sound")) {
        sdk::DocumentGroup music = group(tr("Music"));
        sdk::DocumentGroup effects = group(tr("Sound effects"));
        for (const QJsonValue& value : doc(Doc::Sound).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString label = payload.value(QStringLiteral("label")).toString();
            QStringList ids;
            for (const QJsonValue& request : payload.value(QStringLiteral("requests")).toArray()) {
                ids << QStringLiteral("$%1").arg(request.toInt(), 2, 16, QLatin1Char('0')).toUpper();
            }
            const bool bgm = payload.value(QStringLiteral("kind")).toString() == QLatin1String("bgm");
            (bgm ? music : effects)
                .documents.append(
                    entry(label, label, ids.join(QLatin1Char(' ')),
                          payloadDirty(doc(Doc::Sound), savedDoc(Doc::Sound), QStringLiteral("payloads"), label)));
        }
        // The DAC samples apart: the voices the game asks for, the drums
        // the music's DAC channel plays.
        sdk::DocumentGroup voices = group(tr("Voices"));
        sdk::DocumentGroup drums = group(tr("Drum samples"));
        for (const QJsonValue& value : doc(Doc::Pcm).value(QStringLiteral("descriptors")).toArray()) {
            const QJsonObject item = value.toObject();
            const bool voice = item.value(QStringLiteral("kind")).toString() == QLatin1String("voice");
            const int id = item.value(QStringLiteral("id")).toInt();
            const QString number = QString::number(id, 16).toUpper();
            (voice ? voices : drums)
                .documents.append(entry(voice ? QStringLiteral("voice:") + number : QStringLiteral("drum:") + number,
                                        SampleModel::sampleTitle(voice, id),
                                        item.value(QStringLiteral("bank")).toString()));
        }
        groups << music << effects;
        if (!voices.documents.isEmpty()) groups << voices;
        if (!drums.documents.isEmpty()) groups << drums;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup demos = group(tr("Attract demos"));
        const QJsonArray list = doc(Doc::Demos).value(QStringLiteral("demos")).toArray();
        for (int index = 0; index < list.size(); ++index) {
            const int stage = list.at(index).toObject().value(QStringLiteral("stage")).toInt() / 2;
            demos.documents.append(
                entry(QStringLiteral("demo-%1").arg(index + 1), tr("Demo %1").arg(index + 1), stageLabel(stage),
                      list.at(index) != savedDoc(Doc::Demos).value(QStringLiteral("demos")).toArray().at(index)));
        }
        groups.append(demos);
    } else if (kind == QLatin1String("playtest")) {
        sdk::DocumentGroup session = group(tr("Session"));
        session.documents.append(entry(QStringLiteral("playtest"), tr("Build and run"), stageLabel(playStage_)));
        groups.append(session);
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> AlienSoldierSession::openEditor(const QString& kind, const QString& document) {
    const auto stageOf = [&document]() {
        bool ok = false;
        const int number = document.mid(6).toInt(&ok);
        return ok ? number - 1 : -1;
    };
    // "set-N" (a level), "set-N-a"/"set-N-b" (a block set's plane).
    const auto setOf = [this, &document]() {
        if (!document.startsWith(QLatin1String("set-"))) return -1;
        bool ok = false;
        const int number = document.mid(4).section(QLatin1Char('-'), 0, 0).toInt(&ok);
        return ok && number >= 1 && number <= blockSets().size() ? number - 1 : -1;
    };
    if (kind == QLatin1String("levels")) {
        const int set = setOf();
        if (set >= 0) return std::make_unique<LevelsModel>(this, set, -1);
        if (document.startsWith(QLatin1String("arena-"))) {
            // "arena-SS-N": stage SS's N-th arena.
            const int stage = document.section(QLatin1Char('-'), 1, 1).toInt() - 1;
            const int arena = document.section(QLatin1Char('-'), 2, 2).toInt() - 1;
            if (stage < 0 || stage >= stageCount() || arena < 0 || arena >= stageArenas(stage).size()) return nullptr;
            return std::make_unique<LevelsModel>(this, blockSetOf(stage), stage, arena);
        }
        const int stage = stageOf();
        if (stage < 0 || stage >= stageCount()) return nullptr;
        if (stage < passwordStages()) setPlayStage(stage);
        return std::make_unique<LevelsModel>(this, blockSetOf(stage), stage);
    }
    if (kind == QLatin1String("metatiles")) {
        if (document.startsWith(QLatin1String("obj:")))
            return std::make_unique<ObjectMetatilesModel>(this, document.mid(4));
        const int set = setOf();
        return set < 0 ? nullptr : std::make_unique<MetatilesModel>(this, set, document.endsWith(QLatin1String("-b")));
    }
    if (kind == QLatin1String("tiles")) {
        if (document.startsWith(QLatin1String("c:")))
            return std::make_unique<TilesModel>(this, Doc::Compressed, document.mid(2));
        if (document.startsWith(QLatin1String("u:")))
            return std::make_unique<TilesModel>(this, Doc::Tiles, document.mid(2));
        return nullptr;
    }
    if (kind == QLatin1String("palettes")) {
        // A place, "label#N", or a bare label (the first command) from older views.
        return std::make_unique<PalettesModel>(this, document);
    }
    if (kind == QLatin1String("objects")) {
        if (document.startsWith(QLatin1String("boss:"))) return std::make_unique<BossesModel>(this, document);
        return std::make_unique<ObjectsModel>(this, document);
    }
    if (kind == QLatin1String("sprites")) return std::make_unique<AnimationsModel>(this, document);
    if (kind == QLatin1String("bosses")) {
        if (!document.startsWith(QLatin1String("anim:"))) return nullptr;
        return std::make_unique<BossesModel>(this, document);
    }
    if (kind == QLatin1String("screens")) return std::make_unique<ScreensModel>(this, document);
    if (kind == QLatin1String("text")) return std::make_unique<TextModel>(this, document);
    if (kind == QLatin1String("sound")) {
        if (document.startsWith(QLatin1String("voice:")) || document.startsWith(QLatin1String("drum:")))
            return std::make_unique<SampleModel>(this, document);
        return std::make_unique<SoundModel>(this, document);
    }
    if (kind == QLatin1String("demo")) {
        const int demo = document.mid(5).toInt() - 1;
        return demo < 0 || demo > 3 ? nullptr : std::make_unique<DemoModel>(this, demo);
    }
    if (kind == QLatin1String("playtest")) return std::make_unique<PlaytestModel>(this);
    return nullptr;
}

// ---------------------------------------------------------------- editing

void AlienSoldierSession::setDocument(Doc which, const QJsonObject& value, quint64 revision) {
    Tracked& tracked = documents_.tracked(which);
    tracked.doc = value;
    tracked.revision = revision;
    changed(which);
}

void AlienSoldierSession::changed(Doc which, bool lists) {
    switch (which) {
        case Doc::Levels: {
            // Moving or changing objects leaves the scenes alone.
            const QJsonArray records = documents_.tracked(Doc::Levels).doc.value(QStringLiteral("records")).toArray();
            if (records == sceneRecords_) break;
            sceneRecords_ = records;
            [[fallthrough]];
        }
        case Doc::Maps:
        case Doc::Tiles:
        case Doc::Compressed:
        case Doc::Palettes:
        case Doc::Screens:
            stages_.clear();
            screens_.clear();
            ++sceneGeneration_;
            break;
        default:
            break;
    }
    emit documentChanged(which);
    emit contentChanged();
    if (lists) {
        for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    }
}

bool AlienSoldierSession::edit(Doc which, const QString& text, const std::function<void(QJsonObject&)>& change,
                               QString* error) {
    Tracked& tracked = documents_.tracked(which);
    QJsonObject after = tracked.doc;
    try {
        change(after);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    if (after == tracked.doc) return true;
    const quint64 before = tracked.revision;
    const quint64 next = tracked.nextRevision++;
    undoStack()->push(new sdk::SnapshotCommand<QJsonObject>(
        [this, which](const QJsonObject& value, quint64 revision) { setDocument(which, value, revision); }, tracked.doc,
        before, after, next, text));
    return true;
}

// A stroke's step: the lists (names, unsaved marks) change only when it is
// committed.
void AlienSoldierSession::touch(Doc which) { changed(which, false); }

void AlienSoldierSession::commit(Doc which, const QJsonObject& before, quint64 beforeRevision, const QString& text) {
    Tracked& tracked = documents_.tracked(which);
    if (tracked.doc == before) return;
    const quint64 next = tracked.nextRevision++;
    // The stroke already shows; the first redo of a pushed command applies it again.
    undoStack()->push(new sdk::SnapshotCommand<QJsonObject>(
        [this, which](const QJsonObject& value, quint64 revision) { setDocument(which, value, revision); }, before,
        beforeRevision, tracked.doc, next, text));
}

void AlienSoldierSession::reportMapOverflow(const QString& source, const MapOverflow& full, bool* reported) {
    if (*reported) return;
    *reported = true;
    report(sdk::Severity::Warning, source,
           tr("%1 would take %2 bytes compressed, and its slot in the ROM holds %3: the change is not made.")
               .arg(full.payload)
               .arg(full.size)
               .arg(full.capacity));
}

bool AlienSoldierSession::save(QString* error) {
    bool ok = true;
    for (int index = 0; index < kDocCount; ++index) {
        const Doc which = static_cast<Doc>(index);
        Tracked& tracked = documents_.tracked(which);
        if (!isEditable(which) || !tracked.isDirty()) continue;
        try {
            saveJson(tracked.path, tracked.doc);
            tracked.saved = tracked.doc;
            tracked.savedRevision = tracked.revision;
        } catch (const std::exception& e) {
            if (ok && error)
                *error = tr("Cannot save %1: %2").arg(QFileInfo(tracked.path).fileName(), QString::fromUtf8(e.what()));
            ok = false;
        }
    }
    if (ok) undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    return ok;
}

bool AlienSoldierSession::canRevert(const QString& kind, const QString& document) const {
    Q_UNUSED(document);
    return kind != QLatin1String("playtest");
}

bool AlienSoldierSession::revert(const QString& kind, const QString& document, QString* error) {
    // A view reverts the documents it edits, whole.
    QVector<Doc> docs;
    if (kind == QLatin1String("levels")) docs = {Doc::Levels};
    else if (kind == QLatin1String("metatiles"))
        docs = {document.startsWith(QLatin1String("obj:")) ? Doc::Mappings : Doc::Maps};
    else if (kind == QLatin1String("tiles"))
        docs = {document.startsWith(QLatin1String("c:")) ? Doc::Compressed : Doc::Tiles};
    else if (kind == QLatin1String("palettes")) docs = {Doc::Palettes};
    else if (kind == QLatin1String("objects")) docs = {Doc::Mappings};
    else if (kind == QLatin1String("bosses")) docs = {Doc::Bosses};
    else if (kind == QLatin1String("sprites")) docs = {Doc::Animations, Doc::Mappings};
    else if (kind == QLatin1String("screens")) docs = {Doc::Screens};
    else if (kind == QLatin1String("text")) docs = {Doc::Text};
    else if (kind == QLatin1String("sound")) docs = {Doc::Sound, Doc::Envelopes, Doc::Pcm};
    else if (kind == QLatin1String("demo")) docs = {Doc::Demos};
    for (Doc which : docs) {
        const QJsonObject original = documents_.tracked(which).original;
        if (!edit(which, tr("Revert %1").arg(kind), [&original](QJsonObject& value) { value = original; }, error))
            return false;
    }
    return true;
}

const Scene& AlienSoldierSession::stage(int index) const {
    auto found = stages_.find(index);
    if (found == stages_.end()) found = stages_.insert(index, stageScene(documents_, index));
    return found.value();
}

const Scene& AlienSoldierSession::screen(int index) const {
    auto found = screens_.find(index);
    if (found == screens_.end()) found = screens_.insert(index, screenScene(documents_, index));
    return found.value();
}

// ---------------------------------------------------------------- playing

void AlienSoldierSession::setPlayStage(int stage) {
    stage = std::clamp(stage, 0, passwordStages() - 1);
    if (stage == playStage_) return;
    playStage_ = stage;
    emit playStageChanged();
    emit selectedStartChanged();
    emit documentsChanged(QStringLiteral("playtest"));
}

QString AlienSoldierSession::selectedStart() const {
    return demoStart() ? tr("Demo %1").arg(demo_ + 1) : stageLabel(playStage_);
}

QString AlienSoldierSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

void AlienSoldierSession::build() {
    runBuild([](bool) {}, true);
}

void AlienSoldierSession::play() { launch(-1); }

void AlienSoldierSession::playSelected() {
    if (isRecording()) {
        stopRecording();
    } else if (demoStart()) {
        launchDemo(demo_, false);
    } else {
        launch(playStage_);
    }
}

void AlienSoldierSession::launch(int stage) {
    EmulatorPlan plan;
    plan.force = false;
    if (stage >= 0) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("alien-soldier-playtest-plan"),
                                     project_.path(), QStringLiteral("--stage"), QString::number(stage)};
        plan.label = tr("Plan playtest of %1").arg(stageLabel(stage));
        plan.title = QStringLiteral("Alien Soldier · %1").arg(stageLabel(stage));
        // The playtest script writes one status line once the stage runs.
        plan.readyFile = QStringLiteral("build/playtest-status.txt");
        plan.readyVariable = QStringLiteral("ALIEN_PLAYTEST_RESULT");
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("Alien Soldier · power-on");
    }
    launchPlanned(plan);
}

void AlienSoldierSession::attachDemoEditor(int demo, bool attach) {
    const QString before = selectedStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (attach) demo_ = demo;
    if (selectedStart() != before) emit selectedStartChanged();
}

bool AlienSoldierSession::prepareBuild(BuildStep& step, QString*) {
    // A build takes minutes; Play and the demos reuse a ROM that is current.
    step.upToDate = !step.force && buildIsCurrent();
    return true;
}

bool AlienSoldierSession::buildIsCurrent() const {
    const QDir root(project_.directory());
    const auto sha256 = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        return QString::fromLatin1(hash.result().toHex());
    };
    QFile file(root.filePath(QStringLiteral("build/studio-build-result.json")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject report = QJsonDocument::fromJson(file.readAll()).object();
    if (report.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.studio-build-result") ||
        report.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
        return false;
    }
    if (report.value(QStringLiteral("project")).toObject().value(QStringLiteral("sha256")).toString() !=
        sha256(project_.path())) {
        return false;
    }
    for (const QJsonValue& value : report.value(QStringLiteral("documents")).toArray()) {
        const QJsonObject document = value.toObject();
        if (document.value(QStringLiteral("sha256")).toString() !=
            sha256(root.filePath(document.value(QStringLiteral("path")).toString()))) {
            return false;
        }
    }
    const QJsonObject artifact = report.value(QStringLiteral("artifact")).toObject();
    return artifact.value(QStringLiteral("sha256")).toString() ==
           sha256(root.filePath(artifact.value(QStringLiteral("path")).toString()));
}

void AlienSoldierSession::playDemo(int demo, bool record) {
    if (!isRecording()) launchDemo(demo, record);
}

void AlienSoldierSession::launchDemo(int demo, bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("alien-soldier-demo-plan"), project_.path(),
                                 QStringLiteral("--demo"), QString::number(demo)};
    plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
    plan.title =
        record ? tr("Alien Soldier · recording demo %1").arg(demo + 1) : tr("Alien Soldier · demo %1").arg(demo + 1);
    plan.demo = true;
    plan.force = false;
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("ALIEN_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage =
            tr("Recording: the demo starts the stage itself, then the controller plays it; it ends after the demo's "
               "time, on Start or on Stop recording.");
        plan.recorded = [this, demo](const QString& path) { applyRecording(demo, path); };
    }
    launchPlanned(plan);
}

void AlienSoldierSession::applyRecording(int demo, const QString& path) {
    if (!QFileInfo::exists(path)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    // The kit joins the demo's own start (title and weapon setup) with the
    // recorded steps.
    const QString steps = QDir(project_.directory()).filePath(QStringLiteral("build/demo-steps.json"));
    {
        QFile file(steps);
        if (!file.open(QIODevice::WriteOnly)) {
            report(sdk::Severity::Error, QStringLiteral("demo"), tr("Cannot write %1.").arg(steps));
            return;
        }
        const QJsonArray current = doc(Doc::Demos)
                                       .value(QStringLiteral("demos"))
                                       .toArray()
                                       .at(demo)
                                       .toObject()
                                       .value(QStringLiteral("steps"))
                                       .toArray();
        file.write(QJsonDocument(current).toJson(QJsonDocument::Compact));
    }
    QString error;
    sdk::ProcessSpec spec = host()->kitCommand({QStringLiteral("studio"), QStringLiteral("alien-soldier-demo-steps"),
                                                steps, QStringLiteral("--recording"), path},
                                               project_.directory(), &error);
    if (!error.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    spec.label = tr("Join the recording to demo %1").arg(demo + 1);
    host()->runProcess(spec, [this, demo](const sdk::ProcessResult& result) {
        if (!result.started || result.exitCode != 0) {
            report(sdk::Severity::Error, QStringLiteral("demo"), sdk::failureText(result));
            return;
        }
        const QJsonArray steps =
            QJsonDocument::fromJson(result.standardOutput).object().value(QStringLiteral("steps")).toArray();
        QString failure;
        const bool ok = edit(
            Doc::Demos, tr("Record demo %1").arg(demo + 1),
            [demo, steps](QJsonObject& value) {
                QJsonArray demos = value.value(QStringLiteral("demos")).toArray();
                QJsonObject item = demos.at(demo).toObject();
                item.insert(QStringLiteral("steps"), steps);
                demos.replace(demo, item);
                value.insert(QStringLiteral("demos"), demos);
            },
            &failure);
        if (!ok) {
            report(sdk::Severity::Error, QStringLiteral("demo"), failure);
            return;
        }
        report(sdk::Severity::Info, QStringLiteral("demo"),
               tr("Recorded demo %1: %2 steps.").arg(demo + 1).arg(steps.size()));
    });
}

}  // namespace rl::alien_soldier
