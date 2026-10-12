#include "doraemon/plugin/doraemon_session.hpp"

#include "doraemon/plugin/animation_model.hpp"
#include "doraemon/plugin/block_model.hpp"
#include "doraemon/plugin/demo_model.hpp"
#include "doraemon/plugin/map_model.hpp"
#include "doraemon/plugin/music_model.hpp"
#include "doraemon/plugin/table_models.hpp"
#include "doraemon/plugin/playtest_model.hpp"
#include "doraemon/plugin/route_model.hpp"
#include "doraemon/plugin/screen_model.hpp"
#include "doraemon/plugin/text_model.hpp"
#include "nes/editors.hpp"
#include "nes/palette.hpp"
#include "nes/record_table_model.hpp"
#include "nes/sound_effect_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QUndoStack>

#include <algorithm>

namespace rl::doraemon {

namespace {

using sdk::failureText;
using sdk::loadJsonObject;
using sdk::saveJsonObject;
using sdk::tabDocument;

const QString kPaletteCodec = QStringLiteral("nes.palette-table.v1");

const QString kChrCodec = QStringLiteral("nes.chr-2bpp.v1");

sdk::Kind kind(const char* id, const char* label, const char* icon, sdk::EditorType editor) {
    sdk::Kind result;
    result.id = QString::fromLatin1(id);
    result.label = QString::fromLatin1(label);
    result.icon = QString::fromLatin1(icon);
    result.editor = editor;
    return result;
}

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta = QString(), bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

const QStringList kShellPalettes{QStringLiteral("shell-palette-title"), QStringLiteral("shell-palette-help-1"),
                                 QStringLiteral("shell-palette-help-2"), QStringLiteral("shell-palette-help-3"),
                                 QStringLiteral("shell-palette-chapter"), QStringLiteral("shell-palette-ending")};

QString shellPaletteLabel(const QString& id) {
    if (id == QLatin1String("shell-palette-title")) return rlTr("doraemon", "Title, game over, credits");
    if (id == QLatin1String("shell-palette-chapter")) return rlTr("doraemon", "Chapter start help");
    if (id == QLatin1String("shell-palette-ending")) return rlTr("doraemon", "Ending opening");
    return rlTr("doraemon", "World %1 help").arg(id.right(1));
}

// A snapshot taken before the kit added resources keeps them.
nes::Bundle withResourcesOf(const nes::Bundle& snapshot, const nes::Bundle& current) {
    QJsonArray resources = snapshot.root().value(QStringLiteral("resources")).toArray();
    const QJsonArray now = current.root().value(QStringLiteral("resources")).toArray();
    if (resources.size() >= now.size()) return snapshot;
    for (int i = 0; i < now.size(); ++i) {
        const QString id = now.at(i).toObject().value(QStringLiteral("id")).toString();
        if (!snapshot.contains(id)) resources.insert(std::min(i, int(resources.size())), now.at(i));
    }
    QJsonObject root = snapshot.root();
    root.insert(QStringLiteral("resources"), resources);
    return nes::Bundle::fromJson(root);
}

const QStringList kRoutes{QStringLiteral("Part 1"), QStringLiteral("Part 2"), QStringLiteral("Part 2A"),
                          QStringLiteral("Part 2B"), QStringLiteral("Part 3")};

// "world1-palette-03" -> world 1, preset 3; {0, -1} for other tables.
std::pair<int, int> presetOf(const QString& id) {
    if (!id.startsWith(QLatin1String("world")) || !id.contains(QLatin1String("-palette-"))) return {0, -1};
    return {id.mid(5, 1).toInt(), id.section(QLatin1Char('-'), 2).toInt()};
}

const char* const kSoundDrivers[4] = {"World 1", "World 2", "World 3", "Title and ending"};

// The object tables of each world (kit nes.record-table.v1), in the order
// the Levels view lists them, and the World 3 behavior streams after them.
struct TableEntry {
    World world;
    const char* id;
    const char* title;
    const char* note;
};
const TableEntry kTables[] = {
    {World::One, "world1-underground-rooms", QT_TRANSLATE_NOOP("doraemon", "Underground rooms"),
     QT_TRANSLATE_NOOP("doraemon", "Where each underground room puts the camera and Doraemon, and the city return "
                                   "each way out leads to (— for none).")},
    {World::One, "world1-city-returns", QT_TRANSLATE_NOOP("doraemon", "City returns"),
     QT_TRANSLATE_NOOP("doraemon", "Where Doraemon comes back up into the city: the camera and the manhole.")},
    {World::One, "world1-weapon-sounds", QT_TRANSLATE_NOOP("doraemon", "Weapon sounds"),
     QT_TRANSLATE_NOOP("doraemon", "The sound effect request a shot plays at each weapon level.")},
    {World::One, "world1-weapon-shots", QT_TRANSLATE_NOOP("doraemon", "Weapon shots"),
     QT_TRANSLATE_NOOP("doraemon", "Where a shot starts from Doraemon and how it is drawn, per weapon level and direction.")},
    {World::Two, "world2-enemy-states", QT_TRANSLATE_NOOP("doraemon", "Enemy states"),
     QT_TRANSLATE_NOOP("doraemon", "World 2's enemies by state: how often they attack, the hits they take and their "
                                   "score code. State $00 is no enemy; its damage and score bytes belong to other tables.")},
    {World::Three, "world3-rooms", QT_TRANSLATE_NOOP("doraemon", "Room flags"),
     QT_TRANSLATE_NOOP("doraemon", "Per room: the look type $03 creatures take, whether type $04 creatures follow "
                                   "Doraemon, and whether type $05 creatures stay where they are.")},
    {World::Three, "world3-type05-motion", QT_TRANSLATE_NOOP("doraemon", "Type $05 steps"),
     QT_TRANSLATE_NOOP("doraemon", "How far a type $05 creature moves each step in its four directions.")},
    {World::Three, "world3-type0c-formation", QT_TRANSLATE_NOOP("doraemon", "Type $0C group"),
     QT_TRANSLATE_NOOP("doraemon", "The creatures a type $0C spawn brings: state, position and type.")},
    {World::Three, "world3-type0a-formation", QT_TRANSLATE_NOOP("doraemon", "Type $0A group"),
     QT_TRANSLATE_NOOP("doraemon", "The creatures a type $0A spawn brings: state, position and type.")},
    {World::Three, "world3-type08-formation", QT_TRANSLATE_NOOP("doraemon", "Type $08 group"),
     QT_TRANSLATE_NOOP("doraemon", "The creatures a type $08 spawn brings: state, position, type and first frame.")},
};
const char* const kRoutingIds[4] = {"world1-effect-routing", "world2-effect-routing", "world3-effect-routing",
                                    "shell-effect-routing"};
constexpr int kBehaviorStreams = 16;

// The effect requests of the four bank-local drivers (QueueEffect,
// UpdateEffects): what each one's handler pair does, from the source
// reconstruction's structural roles, and where play starts it when the
// call site says so. Request 0 resets the effects and is not listed.
QVector<nes::SoundEffect> soundEffects(int driver) {
    static const char* const kRoles[26] = {
        nullptr,
        "Pulse 2 tone C",
        "Triangle and noise burst",
        "Pulse 2 tone A",
        "Pulse 2 pitch run, long",
        "Pulse 2 pitch run, short",
        "Pulse 1 alternating burst",
        "Pulse 1 tone",
        "Pulse 1 tone, holds the triangle",
        "Noise ramp",
        "Tonal sequence on all channels",
        "Pulse 2 tone B",
        "Noise sweep, slow",
        "Noise swell",
        "Pulse 2 three steps",
        "Pulse 2 pitch run",
        "Pulse 1 melody",
        "Triangle rise",
        "Pulse 2 two tones",
        "Pulse 2 pitch run, wide",
        "Pulse 2 fade",
        "Pulse 1 alternator (repeats)",
        "Pulse 1 alternator (repeats)",
        "Pulse 1 alternator (repeats)",
        "Pulse 1 alternator (repeats)",
        "Noise sweep, fast"};
    // World 2's driver has fifteen requests in its own order (the game's
    // world2_sound_requests catalog); without it they show by number.
    const QJsonArray world2 = sdk::gameCatalog("world2_sound_requests").toArray();
    struct Known {
        int driver;
        int request;
        const char* name;
    };
    static const Known kKnown[] = {{0, 0x06, "Pause"}, {0, 0x12, "Jump"}, {1, 0x09, "Part complete"},
                                   {1, 0x0A, "Inventory attack"}};
    QVector<nes::SoundEffect> result;
    const int count = driver == 1 ? 15 : 26;
    for (int request = 1; request < count; ++request) {
        const int role = driver == 1 ? (request < world2.size() ? world2.at(request).toInt() : -1) : request;
        QString name = role > 0 && role < 26 ? QString::fromLatin1(kRoles[role]) : rlTr("doraemon", "Effect");
        if (driver == 3 && request == 22) name = rlTr("doraemon", "Pulse 1 and rising noise (repeats)");
        if (driver == 3 && request == 23) name = rlTr("doraemon", "Pulse 1 and falling noise (repeats)");
        if (driver == 3 && request == 24) name = rlTr("doraemon", "Pulse pair and noise tail");
        for (const Known& known : kKnown) {
            if (known.driver == driver && known.request == request) name = QStringLiteral("%1 · %2").arg(QString::fromLatin1(known.name), name);
        }
        // The game's own names for its sounds (sound_effects, "driver:request").
        const QString named = sdk::gameName("sound_effects", QStringLiteral("%1:%2").arg(driver).arg(nes::hex(request)), QString());
        if (!named.isEmpty()) name = QStringLiteral("%1 · %2").arg(named, name);
        result << nes::SoundEffect{QStringLiteral("doraemon:%1:%2").arg(driver).arg(request),
                                   QStringLiteral("%1 %2").arg(nes::hex(request), name),
                                   rlTr("doraemon", "%1 · request %2").arg(QString::fromLatin1(kSoundDrivers[driver]), nes::hex(request)),
                                   name.contains(QLatin1String("(repeats)"))
                                       ? rlTr("doraemon", "It repeats until the game starts another sound; the preview "
                                                          "stops after five seconds.")
                                       : QString()};
    }
    return result;
}

}  // namespace

const QVector<MapEntry>& hierarchyMaps() {
    static const QVector<MapEntry> kMaps{{World::One, QStringLiteral("city"), rlTr("doraemon", "City")},
                                         {World::One, QStringLiteral("underground"), rlTr("doraemon", "Underground")},
                                         {World::Three, QStringLiteral("underwater"), rlTr("doraemon", "Underwater")}};
    return kMaps;
}

// ---------------------------------------------------------------- opening

std::unique_ptr<DoraemonSession> DoraemonSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<DoraemonSession> session(new DoraemonSession);
    session->project_ = project;
    try {
        const QString document = tabDocument(project, QStringLiteral("doraemon.hierarchy.v1"));
        if (document.isEmpty()) throw nes::BundleError("The project has no Doraemon levels tab.");
        session->bundlePath_ = project.resolve(document);
        session->state_.bundle = nes::Bundle::load(session->bundlePath_);
        for (World w : {World::One, World::Three}) {
            if (!session->state_.bundle.contains(hierarchyId(w)) || !session->state_.bundle.contains(chrId(w))) {
                throw nes::BundleError(tr("The resource bundle lacks %1.").arg(hierarchyId(w)).toStdString());
            }
        }
        const QString text = tabDocument(project, QStringLiteral("doraemon.text@1"));
        if (!text.isEmpty()) {
            session->textPath_ = project.resolve(text);
            session->state_.text = loadJsonObject(session->textPath_);
        }
        const QString music = tabDocument(project, QStringLiteral("doraemon.music@1"));
        if (!music.isEmpty()) {
            session->musicPath_ = project.resolve(music);
            session->state_.music = loadJsonObject(session->musicPath_);
        }
        const QString demo = tabDocument(project, QStringLiteral("doraemon.demo@1"));
        if (!demo.isEmpty()) {
            session->demoPath_ = project.resolve(demo);
            session->state_.demo = loadJsonObject(session->demoPath_);
        }
        session->saved_ = session->state_;
        session->painter();
        session->loadWorld2View();
    } catch (const std::exception& e) {
        if (error) *error = tr("Cannot open the Doraemon project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile DoraemonSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Doraemon");
    profile.badge = QStringLiteral("NES");
    profile.platform = tr("Famicom");
    profile.revision = state_.bundle.revision() == QLatin1String("rev_a") ? tr("J Rev A") : QStringLiteral("J");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("doraemon.hierarchy.v1");
    return profile;
}

QVector<sdk::Kind> DoraemonSession::kinds() const {
    return {kind("levels", "Levels", "levels", sdk::EditorType::TileMap),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("metatiles", "Metatiles", "metatiles", sdk::EditorType::TileMap),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("animations", "Animations", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sound", "Sound", "sound", sdk::EditorType::Sequencer),
            kind("demo", "Demo", "play", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> DoraemonSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    const auto changed = [this](const QString& id) {
        return state_.bundle.contains(id) && state_.bundle.resource(id) != saved_.bundle.resource(id);
    };
    const auto group = [&groups](const QString& title) -> sdk::DocumentGroup& {
        for (sdk::DocumentGroup& g : groups) {
            if (g.title == title) return g;
        }
        sdk::DocumentGroup g;
        g.title = title;
        groups << g;
        return groups.last();
    };
    if (kind == QLatin1String("levels")) {
        for (const MapEntry& m : hierarchyMaps()) {
            int width = 0;
            int height = 0;
            const QVector<int> now = mapCellsIn(state_.bundle, m.world, m.id, &width, &height);
            const bool dirty = now != mapCellsIn(saved_.bundle, m.world, m.id) ||
                               (m.world == World::Three && changed(ids::kRoomSelectors));
            group(worldLabel(m.world)).documents
                << entry(QStringLiteral("%1/%2").arg(int(m.world)).arg(m.id), m.label, QStringLiteral("%1 × %2").arg(width).arg(height), dirty);
            if (m.world == World::One && state_.bundle.contains(QStringLiteral("world2-screens"))) {
                // World 2 between the others, its routes in stage order.
                for (int r = 0; r < kRoutes.size(); ++r) {
                    const QJsonArray panels =
                        world2View_.value(QStringLiteral("routes")).toArray().at(r).toObject().value(QStringLiteral("panels")).toArray();
                    if (m.id != QLatin1String("underground")) continue;
                    const QJsonArray nowScreens = state_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("screens")).toArray();
                    const QJsonArray savedScreens = saved_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("screens")).toArray();
                    bool routeDirty = false;
                    for (const QJsonValue& p : panels) {
                        const int selector = p.toObject().value(QStringLiteral("selector")).toInt();
                        routeDirty = routeDirty || nowScreens.at(selector) != savedScreens.at(selector);
                    }
                    group(worldLabel(World::Two)).documents
                        << entry(QStringLiteral("route/%1").arg(r), kRoutes.at(r),
                                 panels.isEmpty() ? QString() : tr("%n screens", nullptr, panels.size()), routeDirty);
                }
            }
        }
        // The object tables; opening one adds them from the project's source.
        for (const TableEntry& table : kTables) {
            const QString id = QString::fromLatin1(table.id);
            const bool present = state_.bundle.contains(id);
            group(tr("%1 · tables").arg(worldLabel(table.world))).documents
                << entry(QStringLiteral("table:") + id, rlTr("doraemon", table.title),
                         present ? tr("%n rows", nullptr, int(state_.bundle.resource(id).value(QStringLiteral("records")).toArray().size()))
                                 : tr("not added yet"),
                         present && saved_.bundle.contains(id) && changed(id));
        }
        const bool behavior = state_.bundle.contains(QStringLiteral("world3-behavior"));
        for (int s = 0; s < kBehaviorStreams; ++s) {
            const QJsonArray now = behavior ? state_.bundle.resource(QStringLiteral("world3-behavior")).value(QStringLiteral("streams")).toArray() : QJsonArray();
            const QJsonArray saved = behavior && saved_.bundle.contains(QStringLiteral("world3-behavior"))
                                         ? saved_.bundle.resource(QStringLiteral("world3-behavior")).value(QStringLiteral("streams")).toArray()
                                         : QJsonArray();
            group(tr("%1 · behavior").arg(worldLabel(World::Three))).documents
                << entry(QStringLiteral("behavior:%1").arg(s), tr("Behavior %1").arg(s),
                         behavior ? tr("%n steps", nullptr, int(now.at(s).toObject().value(QStringLiteral("instructions")).toArray().size()))
                                  : tr("not added yet"),
                         behavior && now.at(s) != saved.at(s));
        }
    } else if (kind == QLatin1String("graphics")) {
        static const char* const kBanks[4] = {"World 1", "World 2", "World 3", "Title and ending"};
        for (int bank = 0; bank < 4; ++bank) {
            const QString id = QStringLiteral("chr-bank-%1").arg(bank);
            if (!state_.bundle.contains(id)) continue;
            sdk::DocumentGroup& g = group(tr("Bank %1 · %2").arg(bank).arg(QLatin1String(kBanks[bank])));
            for (int half = 0; half < 2; ++half) {
                bool dirty = false;
                for (int t = half * 256; t < half * 256 + 256 && !dirty; ++t) {
                    dirty = state_.bundle.tilePixels(id, t) != saved_.bundle.tilePixels(id, t);
                }
                g.documents << entry(QStringLiteral("%1/%2").arg(id).arg(half),
                                     half == 0 ? tr("Sprites") : tr("Background"),
                                     tr("256 tiles"), dirty);
            }
        }
    } else if (kind == QLatin1String("metatiles")) {
        for (World w : {World::One, World::Three}) {
            sdk::DocumentGroup& g = group(worldLabel(w));
            g.documents << entry(QStringLiteral("small/%1").arg(int(w)), tr("Small blocks"), QStringLiteral("16×16"),
                                 changed(hierarchyId(w)));
            g.documents << entry(QStringLiteral("big/%1").arg(int(w)), tr("Big blocks"), QStringLiteral("32×32"),
                                 changed(hierarchyId(w)));
        }
        if (state_.bundle.contains(ids::kWorld2Metatiles)) {
            group(worldLabel(World::Two)).documents
                << entry(QStringLiteral("metatiles/2"), tr("Metatiles"), QStringLiteral("16×16"), changed(ids::kWorld2Metatiles));
        }
        // Keep the worlds in order.
        std::sort(groups.begin(), groups.end(), [](const sdk::DocumentGroup& a, const sdk::DocumentGroup& b) { return a.title < b.title; });
    } else if (kind == QLatin1String("palettes")) {
        for (World w : {World::One, World::Two, World::Three}) {
            for (const QString& id : paletteIds(state_.bundle, w)) {
                group(worldLabel(w)).documents
                    << entry(id, tr("Preset %1").arg(presetOf(id).second),
                             tr("%n colours", nullptr, state_.bundle.tableValues(id).size()),
                             state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
            }
        }
        for (const QString& id : kShellPalettes) {
            const bool present = state_.bundle.contains(id);
            group(tr("Title and help")).documents
                << entry(id, shellPaletteLabel(id), present ? tr("32 colours") : tr("not added yet"),
                         present && saved_.bundle.contains(id) && state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
        }
    } else if (kind == QLatin1String("animations")) {
        // Opening one adds the catalogs from the project's own source.
        const bool missing = !state_.bundle.contains(QStringLiteral("world2-enemy-metasprites"));
        const int animations = doraemonAnimations().size();
        for (int i = 0; i < animations; ++i) {
            const DoraemonAnimation a = resolvedAnimation(this, i);
            const QString id = metaspriteId(a.world);
            const bool dirty = !missing && state_.bundle.resource(id) != saved_.bundle.resource(id);
            group(QStringLiteral("%1 · %2").arg(worldLabel(a.world), a.group)).documents
                << entry(QStringLiteral("anim:%1").arg(i), a.label,
                         missing ? tr("not added yet")
                                 : a.frames.size() > 1 ? tr("%1 fr").arg(a.frames.size()) : QString(),
                         dirty && a.group == QLatin1String("Frames"));
        }
    } else if (kind == QLatin1String("screens")) {
        // Opening one adds what the screens need from the project's own source.
        const bool missing = state_.text.isEmpty() || !state_.bundle.contains(QStringLiteral("shell-help-sprites-3"));
        sdk::DocumentGroup& g = group(tr("Screens"));
        for (const QString& id : shell::screenIds()) {
            g.documents << entry(id, shell::screenLabel(id), missing ? tr("not added yet") : QStringLiteral("32 × 30"),
                                 !missing && shell::cells(state_.text, id) != shell::cells(saved_.text, id));
        }
    } else if (kind == QLatin1String("sound")) {
        // Opening one adds the document from the project's own source.
        const bool missing = state_.music.isEmpty();
        static const int kTracks[4] = {8, 6, 8, 4};
        for (int d = 0; d < 4; ++d) {
            const QJsonArray now = state_.music.value(QStringLiteral("drivers")).toArray().at(d).toObject().value(QStringLiteral("segments")).toArray();
            const QJsonArray saved = saved_.music.value(QStringLiteral("drivers")).toArray().at(d).toObject().value(QStringLiteral("segments")).toArray();
            for (int t = 0; t < kTracks[d]; ++t) {
                group(QString::fromLatin1(kSoundDrivers[d])).documents
                    << entry(QStringLiteral("track:%1:%2").arg(d).arg(t), DoraemonMusicModel::trackTitle(d, t),
                             missing ? tr("not added yet") : tr("4 ch"), !missing && now != saved);
            }
        }
        // The music first, then each driver's effects.
        for (int d = 0; d < 4; ++d) {
            sdk::DocumentGroup& effects = group(tr("Sound effects · %1").arg(QString::fromLatin1(kSoundDrivers[d])));
            for (const nes::SoundEffect& effect : soundEffects(d)) effects.documents << entry(QStringLiteral("sfx:") + effect.key, effect.title);
        }
        sdk::DocumentGroup& routing = group(tr("Effect routing"));
        for (int d = 0; d < 4; ++d) {
            const QString id = QString::fromLatin1(kRoutingIds[d]);
            routing.documents << entry(QStringLiteral("routing:%1").arg(d), QString::fromLatin1(kSoundDrivers[d]),
                                       state_.bundle.contains(id) ? QString() : tr("not added yet"),
                                       state_.bundle.contains(id) && saved_.bundle.contains(id) && changed(id));
        }
        sdk::DocumentGroup& bytes = group(tr("Stream bytes"));
        for (int d = 0; d < 4; ++d) {
            const QJsonValue now = state_.music.value(QStringLiteral("drivers")).toArray().at(d);
            const QJsonValue saved = saved_.music.value(QStringLiteral("drivers")).toArray().at(d);
            bytes.documents << entry(QStringLiteral("events:%1").arg(d), QString::fromLatin1(kSoundDrivers[d]),
                                     missing ? tr("not added yet") : QString(), !missing && now != saved);
        }
    } else if (kind == QLatin1String("text")) {
        // Opening one adds the document from the project's own source.
        const bool missing = state_.text.isEmpty();
        const bool dirty = !missing && state_.text != saved_.text;
        sdk::DocumentGroup& g = group(QStringLiteral("Text"));
        g.documents << entry(QStringLiteral("messages"), tr("Title, game over and help"),
                             missing ? tr("not added yet") : QString(), dirty);
        g.documents << entry(QStringLiteral("credits"), tr("Ending credits"),
                             missing ? tr("not added yet") : tr("380 rows"), dirty);
    } else if (kind == QLatin1String("demo")) {
        // Opening it adds the document from the project's own source.
        group(tr("Attract mode")).documents
            << entry(QStringLiteral("world1"), tr("World 1 demo"),
                     state_.demo.isEmpty() ? tr("not added yet")
                                           : tr("%n steps", nullptr, state_.demo.value(QStringLiteral("steps")).toArray().size()),
                     !state_.demo.isEmpty() && state_.demo != saved_.demo);
    } else if (kind == QLatin1String("playtest")) {
        group(tr("Session")).documents
            << entry(QStringLiteral("playtest"), tr("Build and run"), lastBuild().isEmpty() ? QString() : QStringLiteral("built"));
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> DoraemonSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("levels")) {
            for (const MapEntry& m : hierarchyMaps()) {
                if (document == QStringLiteral("%1/%2").arg(int(m.world)).arg(m.id)) {
                    // Map objects came with a newer kit.
                    if (m.world != World::Two && (!state_.bundle.contains(QStringLiteral("world3-schedule")) ||
                                                  !hasWorld3Tables(this))) {
                        upgradeDocuments(kind, document);
                        return nullptr;
                    }
                    return std::make_unique<DoraemonMapModel>(this, m.world, m.id, m.label);
                }
            }
        }
        if (kind == QLatin1String("levels") && document.startsWith(QLatin1String("route/"))) {
            const int route = document.mid(6).toInt();
            if (route < 0 || route >= kRoutes.size()) return nullptr;
            if (!state_.bundle.contains(QStringLiteral("world3-schedule"))) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            if (world2View_.isEmpty() && !loadWorld2View()) {
                computeWorld2View(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonRouteModel>(this, route);
        }
        if (kind == QLatin1String("levels") && document.startsWith(QLatin1String("table:"))) {
            const QString id = document.mid(6);
            for (const TableEntry& table : kTables) {
                if (id != QLatin1String(table.id)) continue;
                if (!state_.bundle.contains(id)) {
                    upgradeDocuments(kind, document);
                    return nullptr;
                }
                return std::make_unique<nes::RecordTableModel>(this, id, rlTr("doraemon", table.title),
                                                               rlTr("doraemon", table.note));
            }
            return nullptr;
        }
        if (kind == QLatin1String("levels") && document.startsWith(QLatin1String("behavior:"))) {
            const int stream = document.mid(9).toInt();
            if (stream < 0 || stream >= kBehaviorStreams) return nullptr;
            if (!state_.bundle.contains(QStringLiteral("world3-behavior"))) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonBehaviorModel>(this, stream);
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("routing:"))) {
            const int driver = document.mid(8).toInt();
            if (driver < 0 || driver > 3) return nullptr;
            const QString id = QString::fromLatin1(kRoutingIds[driver]);
            if (!state_.bundle.contains(id)) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<nes::RecordTableModel>(
                this, id, tr("Effect routing · %1").arg(QString::fromLatin1(kSoundDrivers[driver])),
                tr("Which effect each request of this driver plays. The effect's place in the list is also its "
                   "priority: a playing effect is cut off only by a request whose effect comes earlier, or "
                   "by the same one. Picking an effect another request has swaps the two; request $00 "
                   "resets the sound."));
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("events:"))) {
            const int driver = document.mid(7).toInt();
            if (driver < 0 || driver > 3) return nullptr;
            if (state_.music.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonStreamEventsModel>(this, driver);
        }
        if (kind == QLatin1String("graphics")) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (state_.bundle.resourceIds(kChrCodec).contains(id) && (half == 0 || half == 1)) {
                return std::make_unique<nes::ChrTileModel>(
                    this, id, half * 256, 256,
                    tr("Bank %1 · %2").arg(id.section(QLatin1Char('-'), 2)).arg(half == 0 ? QStringLiteral("sprites")
                                                                                                    : QStringLiteral("background")),
                    half == 0);
            }
        }
        if (kind == QLatin1String("metatiles")) {
            const QString what = document.section(QLatin1Char('/'), 0, 0);
            const World world = World(document.section(QLatin1Char('/'), 1).toInt());
            if (what == QLatin1String("small")) return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Small, world);
            if (what == QLatin1String("big")) return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Big, world);
            if (what == QLatin1String("metatiles") && state_.bundle.contains(ids::kWorld2Metatiles)) {
                return std::make_unique<DoraemonBlockModel>(this, DoraemonBlockModel::Kind::Metatile, World::Two);
            }
        }
        if (kind == QLatin1String("palettes") && state_.bundle.resourceIds(kPaletteCodec).contains(document) &&
            !kShellPalettes.contains(document)) {
            const auto [world, preset] = presetOf(document);
            // World 2's preview is a route screen, laid out by the kit.
            if (world == int(World::Two) && state_.bundle.contains(QStringLiteral("world3-schedule")) &&
                !world2ViewFailed_ && world2View_.isEmpty() && !loadWorld2View()) {
                computeWorld2View(kind, document);
                return nullptr;
            }
            return std::make_unique<nes::PaletteTableModel>(this, document, tr("World %1 · preset %2").arg(world).arg(preset));
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("sfx:"))) {
            for (const nes::SoundEffect& effect : soundEffects(document.section(QLatin1Char(':'), 2, 2).toInt())) {
                if (QStringLiteral("sfx:") + effect.key == document) {
                    return std::make_unique<nes::SoundEffectModel>(this, effect, project_.path(), project_.directory());
                }
            }
            return nullptr;
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("track:"))) {
            if (state_.music.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonMusicModel>(this, document.section(QLatin1Char(':'), 1, 1).toInt(),
                                                        document.section(QLatin1Char(':'), 2, 2).toInt());
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:"))) {
            const int index = document.mid(5).toInt();
            if (index < 0 || index >= doraemonAnimations().size()) return nullptr;
            if (!state_.bundle.contains(QStringLiteral("world2-enemy-metasprites")) || !hasWorld3Tables(this)) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonAnimationModel>(this, index);
        }
        if (kind == QLatin1String("screens") && shell::screenIds().contains(document)) {
            if (state_.text.isEmpty() || !state_.bundle.contains(QStringLiteral("shell-help-sprites-3"))) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonScreenModel>(this, document);
        }
        if (kind == QLatin1String("palettes") && kShellPalettes.contains(document)) {
            if (!state_.bundle.contains(document)) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<nes::PaletteTableModel>(this, document, shellPaletteLabel(document));
        }
        if (kind == QLatin1String("text") && (document == QLatin1String("messages") || document == QLatin1String("credits"))) {
            if (state_.text.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonTextModel>(this, document == QLatin1String("credits"));
        }
        if (kind == QLatin1String("demo") && document == QLatin1String("world1")) {
            if (state_.demo.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<DoraemonDemoModel>(this);
        }
        if (kind == QLatin1String("playtest")) return std::make_unique<DoraemonPlaytestModel>(this);
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const DoraemonPainter& DoraemonSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<DoraemonPainter>(state_.bundle);
        painterRevision_ = revision_;
    }
    return *painter_;
}

void DoraemonSession::setState(const DoraemonState& state, quint64 revision) {
    const DoraemonState current = state_;
    state_ = state;
    // Snapshots from before the kit added documents and resources keep them.
    if (state_.text.isEmpty()) state_.text = current.text;
    if (state_.music.isEmpty()) state_.music = current.music;
    if (state_.demo.isEmpty()) state_.demo = current.demo;
    state_.bundle = withResourcesOf(state_.bundle, current.bundle);
    revision_ = revision;
    emit contentChanged();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
}

bool DoraemonSession::edit(const QString& label, const DoraemonState& next, QString*) {
    if (next == state_) return true;
    undoStack()->push(new sdk::SnapshotCommand<DoraemonState>(
        [this](const DoraemonState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, label));
    return true;
}

bool DoraemonSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) {
    DoraemonState next = state_;
    try {
        change(next.bundle);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return edit(text, next, error);
}

bool DoraemonSession::editText(const QString& label, const QJsonObject& text, QString* error) {
    if (state_.text.isEmpty()) return false;
    DoraemonState next = state_;
    next.text = text;
    return edit(label, next, error);
}

bool DoraemonSession::editMusic(const QString& label, const QJsonObject& music, QString* error) {
    if (state_.music.isEmpty()) return false;
    DoraemonState next = state_;
    next.music = music;
    return edit(label, next, error);
}

QString DoraemonSession::writeMusicSnapshot(QString* error) const {
    const QString path = project_.resolve(QStringLiteral("build/sound-preview/doraemon-music.json"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    try {
        saveJsonObject(path, state_.music);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return {};
    }
    return path;
}

QJsonObject DoraemonSession::musicTrace(int driver, int track) {
    // The document's hash, worked out once per revision.
    if (musicHashRevision_ != revision_ || musicHash_.isEmpty()) {
        musicHash_ = QString::fromLatin1(
            QCryptographicHash::hash(QJsonDocument(state_.music).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex().left(16));
        musicHashRevision_ = revision_;
    }
    const QString hash = musicHash_;
    const QString key = QStringLiteral("%1/%2/%3").arg(hash).arg(driver).arg(track);
    const auto found = traces_.constFind(key);
    if (found != traces_.constEnd()) return *found;
    sdk::Host* h = host();
    if (!h || h->isBusy() || tracing_ == key) return {};
    QString error;
    const QString document = writeMusicSnapshot(&error);
    if (document.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return {};
    }
    const QString output = project_.resolve(QStringLiteral("build/sound-preview/trace-%1-%2.json").arg(driver).arg(track));
    sdk::ProcessSpec spec = h->kitCommand({QStringLiteral("audio"), QStringLiteral("trace-doraemon"), project_.path(),
                                           QStringLiteral("--document"), document, QStringLiteral("--driver"), QString::number(driver),
                                           QStringLiteral("--track"), QString::number(track), QStringLiteral("--frames"),
                                           QString::number(DoraemonMusicModel::kFrames), QStringLiteral("--output"), output},
                                          project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return {};
    }
    spec.label = tr("Trace %1").arg(DoraemonMusicModel::trackTitle(driver, track));
    tracing_ = key;
    h->runProcess(spec, [this, key, output](const sdk::ProcessResult& result) {
        tracing_.clear();
        if (!result.succeeded()) {
            if (!result.cancelled) report(sdk::Severity::Error, QStringLiteral("sound"), tr("Tracing the track failed: %1").arg(failureText(result)));
            return;
        }
        try {
            traces_.insert(key, loadJsonObject(output));
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, QStringLiteral("sound"), QString::fromUtf8(e.what()));
            return;
        }
        emit traceReady();
    });
    return {};
}

bool DoraemonSession::save(QString* error) {
    try {
        if (state_.bundle != saved_.bundle) state_.bundle.save(bundlePath_);
        if (state_.text != saved_.text && !textPath_.isEmpty()) saveJsonObject(textPath_, state_.text);
        if (state_.music != saved_.music && !musicPath_.isEmpty()) saveJsonObject(musicPath_, state_.music);
        if (state_.demo != saved_.demo && !demoPath_.isEmpty()) saveJsonObject(demoPath_, state_.demo);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    saved_ = state_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    return true;
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> DoraemonSession::previewPalettes(const QString& chr, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const int bank = chr.section(QLatin1Char('-'), 2).toInt();
    if (bank >= 0 && bank <= 2) {
        const World world = World(bank + 1);
        const QStringList presets = paletteIds(state_.bundle, world);
        for (int p = 0; p < presets.size(); ++p) {
            const QVector<int> values = state_.bundle.tableValues(presets.at(p));
            for (int row = 0; row < 4; ++row) {
                const int at = (sprites && values.size() >= 32 ? 16 : 0) + row * 4;
                if (at + 4 <= values.size()) {
                    result << nes::PreviewPalette{tr("Preset %1 %2 %3").arg(p).arg(sprites ? QStringLiteral("SPR")
                                                                                                         : QStringLiteral("BG")).arg(row),
                                                  values.mid(at, 4)};
                }
            }
        }
    }
    return result;
}

QStringList DoraemonSession::paletteRowLabels(const QString& table) const {
    QStringList labels;
    const int rows = state_.bundle.contains(table) ? state_.bundle.tableValues(table).size() / 4 : 0;
    for (int r = 0; r < rows; ++r) labels << (r < 4 ? tr("BG %1") : tr("SPR %1")).arg(r % 4);
    return labels;
}

QVector<sdk::PaletteSample> DoraemonSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    if (kShellPalettes.contains(table)) {
        // The screens drawn in this palette; the chapter start shows the help.
        if (state_.text.isEmpty()) return result;
        QStringList screens;
        if (table == QLatin1String("shell-palette-title")) screens = QStringList{QStringLiteral("title")};
        else if (table == QLatin1String("shell-palette-chapter")) screens = QStringList{QStringLiteral("help-1"), QStringLiteral("help-2"), QStringLiteral("help-3")};
        else screens = QStringList{table.mid(int(QStringLiteral("shell-palette-").size()))};
        for (const QString& id : screens) {
            sdk::PaletteSample sample;
            sample.label = shell::screenLabel(id);
            sample.caption = tr("The %1").arg(shell::screenLabel(id).toLower());
            sample.image = shell::render(this, id, state_.text, table);
            result << sample;
        }
        return result;
    }
    const auto [worldNumber, preset] = presetOf(table);
    if (preset < 0) return result;
    const World world = World(worldNumber);
    const DoraemonPainter& art = painter();
    if (world == World::Two) {
        // A route screen drawn in this preset, as Levels shows it: the first
        // panel of a route that uses it, 16x15 metatiles. Spawn cells show
        // the empty metatile under the enemy.
        // The view is a cache file the kit wrote when World 2 was first opened.
        if (world2View_.isEmpty()) const_cast<DoraemonSession*>(this)->loadWorld2View();
        const QJsonArray screens = state_.bundle.contains(QStringLiteral("world2-screens"))
                                       ? state_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("screens")).toArray()
                                       : QJsonArray();
        for (const QJsonValue& routeValue : world2View_.value(QStringLiteral("routes")).toArray()) {
            const QJsonObject route = routeValue.toObject();
            for (const QJsonValue& panelValue : route.value(QStringLiteral("panels")).toArray()) {
                const QJsonObject panel = panelValue.toObject();
                if (panel.value(QStringLiteral("palette")).toInt() != preset) continue;
                const QJsonArray screen = screens.at(panel.value(QStringLiteral("selector")).toInt()).toArray();
                const QJsonArray rows = panel.value(QStringLiteral("cells")).toArray();
                const QJsonArray fixed = panel.value(QStringLiteral("fixed")).toArray();
                QImage image(16 * 16, 15 * 16, QImage::Format_ARGB32);
                image.fill(Qt::black);
                QPainter p(&image);
                for (int y = 0; y < 15; ++y) {
                    for (int x = 0; x < 16; ++x) {
                        const QJsonArray cell = rows.at(y).toArray().at(x).toArray();
                        const int value = cell.size() == 2
                                              ? screen.at(cell.at(1).toInt()).toArray().at(cell.at(0).toInt()).toInt(0xEF)
                                              : fixed.at(y).toArray().at(x).toInt(0xEF);
                        if (value < 0xD0) p.drawImage(x * 16, y * 16, art.metatile(value, preset));
                        else if (value < 0xEF) p.drawImage(x * 16, y * 16, art.metatile(0, preset));
                    }
                }
                p.end();
                sdk::PaletteSample sample;
                sample.label = route.value(QStringLiteral("name")).toString();
                sample.caption = tr("A screen of %1 in preset %2").arg(sample.label).arg(preset);
                sample.image = image;
                result << sample;
                return result;
            }
        }
        // Before the World 2 view is read: the metatiles in this preset,
        // sixteen to a row.
        const int count = state_.bundle.contains(ids::kWorld2Metatiles)
                              ? state_.bundle.resource(ids::kWorld2Metatiles).value(QStringLiteral("tiles")).toArray().size()
                              : 0;
        QImage image(16 * 16, ((count + 15) / 16) * 16, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter p(&image);
        for (int m = 0; m < count; ++m) p.drawImage((m % 16) * 16, (m / 16) * 16, art.metatile(m, preset));
        p.end();
        sdk::PaletteSample sample;
        sample.label = tr("Metatiles");
        sample.caption = tr("World 2's metatiles in preset %1").arg(preset);
        sample.image = image;
        result << sample;
        return result;
    }
    // An 8x8-block view of the first map area the game draws in this preset.
    for (const MapEntry& m : hierarchyMaps()) {
        if (m.world != world) continue;
        int width = 0;
        int height = 0;
        const QVector<int> cells = mapCellsIn(state_.bundle, world, m.id, &width, &height);
        for (int i = 0; i < cells.size(); ++i) {
            const int x = i % width;
            const int y = i / width;
            if (areaPreset(state_.bundle, world, m.id, x, y) != preset) continue;
            const int left = std::clamp(x - x % 8, 0, std::max(0, width - 8));
            const int top = std::clamp(y - y % 8, 0, std::max(0, height - 8));
            QImage image(8 * 32, std::min(8, height) * 32, QImage::Format_ARGB32);
            image.fill(Qt::black);
            QPainter p(&image);
            for (int row = 0; row < std::min(8, height); ++row) {
                for (int column = 0; column < 8; ++column) {
                    const int at = (top + row) * width + left + column;
                    p.drawImage(column * 32, row * 32,
                                art.bigBlock(world, cells.value(at), areaPreset(state_.bundle, world, m.id, left + column, top + row)));
                }
            }
            p.end();
            sdk::PaletteSample sample;
            sample.label = m.label;
            sample.caption = tr("%1 at %2, %3").arg(m.label).arg(left).arg(top);
            sample.image = image;
            result << sample;
            return result;
        }
    }
    return result;
}

// ---------------------------------------------------------------- documents

void DoraemonSession::upgradeDocuments(const QString& kind, const QString& document) {
    UpgradeStep step;
    step.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("doraemon-upgrade"), project_.path()};
    step.label = tr("Add what this kit edits to the project");
    step.source = kind;
    step.failure = tr("Adding the documents failed: %1");
    step.saveFirst = true;
    step.adopt = [this] {
        // The new document joins the saved state; other unsaved edits stay.
        const QString text = tabDocument(project_, QStringLiteral("doraemon.text@1"));
        if (!text.isEmpty() && state_.text.isEmpty()) {
            textPath_ = project_.resolve(text);
            state_.text = saved_.text = loadJsonObject(textPath_);
        }
        const QString music = tabDocument(project_, QStringLiteral("doraemon.music@1"));
        if (!music.isEmpty() && state_.music.isEmpty()) {
            musicPath_ = project_.resolve(music);
            state_.music = saved_.music = loadJsonObject(musicPath_);
        }
        const QString demo = tabDocument(project_, QStringLiteral("doraemon.demo@1"));
        if (!demo.isEmpty() && state_.demo.isEmpty()) {
            demoPath_ = project_.resolve(demo);
            state_.demo = saved_.demo = loadJsonObject(demoPath_);
        }
        // Resources the bundle gained (saved before the upgrade ran).
        const nes::Bundle reloaded = nes::Bundle::load(bundlePath_);
        if (reloaded != state_.bundle) state_.bundle = saved_.bundle = reloaded;
        // A new revision: the room painter and the music hash are kept per revision.
        revision_ = nextRevision_++;
    };
    step.done = tr("Added the documents and resources this kit edits to the project.");
    step.openKind = kind;
    step.openDocument = document;
    runUpgrade(step);
}

// ---------------------------------------------------------------- World 2 view

QString DoraemonSession::world2ViewPath() const {
    // Named after what the geometry depends on, so a stale view is never read.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const char* id : {"world2-stage", "world2-branches", "world2-chapters"}) {
        if (state_.bundle.contains(QString::fromLatin1(id))) {
            hash.addData(QJsonDocument(state_.bundle.resource(QString::fromLatin1(id))).toJson(QJsonDocument::Compact));
        }
    }
    if (state_.bundle.contains(QStringLiteral("world2-screens"))) {
        hash.addData(state_.bundle.resource(QStringLiteral("world2-screens")).value(QStringLiteral("raw_hex")).toString().toLatin1());
    }
    return project_.resolve(QStringLiteral("build/world2-view-%1.json").arg(QString::fromLatin1(hash.result().toHex().left(16))));
}

bool DoraemonSession::loadWorld2View() {
    QFile file(world2ViewPath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject view = QJsonDocument::fromJson(file.readAll()).object();
    if (view.value(QStringLiteral("schema")).toString() != QLatin1String("retrolarium.doraemon-world2-view")) return false;
    world2View_ = view;
    return true;
}

void DoraemonSession::computeWorld2View(const QString& kind, const QString& document) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) return;
    const QString output = world2ViewPath();
    QDir().mkpath(QFileInfo(output).absolutePath());
    QString error;
    sdk::ProcessSpec spec = h->kitCommand({QStringLiteral("studio"), QStringLiteral("doraemon-world2-view"), bundlePath_,
                                           QStringLiteral("--output"), output},
                                          project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, kind, error);
        return;
    }
    spec.label = tr("Lay out the World 2 routes");
    h->runProcess(spec, [this, kind, document](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !loadWorld2View()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, kind, tr("Laying out the World 2 routes failed: %1").arg(failureText(result)));
            }
            if (kind == QLatin1String("palettes")) {
                world2ViewFailed_ = true;
                emit documentRequested(kind, document);
            }
            return;
        }
        emit documentsChanged(QStringLiteral("levels"));
        emit documentRequested(kind, document);
    });
}

// ---------------------------------------------------------------- build and play

void DoraemonSession::build() {
    runBuild([](bool) {});
}

void DoraemonSession::play() {
    launchWorld(0);
}

void DoraemonSession::playSelected() {
    if (isRecording()) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launchWorld(startWorld_);
}

// ---------------------------------------------------------------- demo

int DoraemonSession::demoCapacity() const {
    const QJsonObject layout = state_.demo.value(QStringLiteral("layout")).toObject();
    return (layout.value(QStringLiteral("limit_cpu")).toInt() - layout.value(QStringLiteral("stream_cpu")).toInt()) / 2 - 1;
}

bool DoraemonSession::editDemo(const QString& label, const QJsonArray& steps, QString* error) {
    if (state_.demo.isEmpty() || steps.isEmpty() || steps.size() > demoCapacity()) {
        if (error) *error = tr("The demo holds 1-%1 steps.").arg(demoCapacity());
        return false;
    }
    DoraemonState next = state_;
    next.demo.insert(QStringLiteral("steps"), steps);
    return edit(label, next, error);
}

void DoraemonSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) emit selectedStartChanged();
}

void DoraemonSession::playDemo(bool record) {
    if (!isRecording()) launchDemo(record);
}

QJsonArray DoraemonSession::stepsFromRecording(const QString& text, int capacity) {
    static const struct {
        const char* id;
        int bit;
    } kButtons[] = {{"a", 0x80}, {"b", 0x40}, {"up", 0x08}, {"down", 0x04}, {"left", 0x02}, {"right", 0x01}};
    QJsonArray result;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() != 2 || result.size() >= capacity) continue;
        const int value = words.at(0).toInt();
        const int updates = words.at(1).toInt();
        if (value < 0 || value > 255 || updates < 1 || updates > 256) continue;
        QJsonArray buttons;
        for (const auto& b : kButtons) {
            if (value & b.bit) buttons.append(QString::fromLatin1(b.id));
        }
        result.append(QJsonObject{{QStringLiteral("index"), result.size()},
                                  {QStringLiteral("frames"), updates},
                                  {QStringLiteral("buttons"), buttons}});
    }
    return result;
}

QString DoraemonSession::selectedStart() const {
    if (demoStart()) return tr("World 1 demo");
    if (startBlock_.x() >= 0) {
        return tr("World 1 · %1 %2, %3")
            .arg(startUnderground_ ? QStringLiteral("underground") : QStringLiteral("block"))
            .arg(startBlock_.x())
            .arg(startBlock_.y());
    }
    if (startStage_ >= 0) return tr("World 2 · Part %1").arg(startStage_ + 1);
    if (startRoom_ > 0) return tr("World 3 · room %1").arg(startRoom_);
    return tr("World %1").arg(startWorld_);
}

void DoraemonSession::setStartWorld(int world) {
    const int next = std::clamp(world, 1, 3);
    if (next == startWorld_ && !hasStartPoint()) return;
    startWorld_ = next;
    startBlock_ = QPoint(-1, -1);
    startUnderground_ = false;
    startStage_ = -1;
    startRoom_ = 0;
    emit selectedStartChanged();
}

void DoraemonSession::setStartBlock(QPoint block, bool underground) {
    if (startWorld_ == 1 && block == startBlock_ && underground == startUnderground_) return;
    setStartWorld(1);
    startBlock_ = block;
    startUnderground_ = underground;
    emit selectedStartChanged();
}

void DoraemonSession::setStartStage(int stage) {
    if (startWorld_ == 2 && stage == startStage_) return;
    setStartWorld(2);
    startStage_ = std::clamp(stage, 0, 2);
    emit selectedStartChanged();
}

void DoraemonSession::setStartRoom(int room) {
    if (startWorld_ == 3 && room == startRoom_) return;
    setStartWorld(3);
    startRoom_ = std::clamp(room, 0, 63);
    emit selectedStartChanged();
}

QString DoraemonSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : tr("Play %1").arg(selectedStart());
}

void DoraemonSession::launchDemo(bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("doraemon-demo-plan"), project_.path()};
    plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
    plan.title = record ? tr("Doraemon · recording the demo") : tr("Doraemon · attract demo");
    plan.demo = true;
    // The Playtest view covers the boot and the title.
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("DORAEMON_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage = tr("Recording: when World 1's demo starts Doraemon follows the controller until "
                                   "you press Start or Stop recording, the game ends the demo, or its steps "
                                   "are full.");
        plan.recorded = [this](const QString& path) { applyRecording(path); };
    }
    plan.prepare = [](sdk::ProcessSpec& game) {
        for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, "nes");
    };
    launchPlanned(plan);
}

void DoraemonSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const QJsonArray steps = stepsFromRecording(QString::fromUtf8(file.readAll()), demoCapacity());
    if (steps.isEmpty()) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               tr("The demo ended before Doraemon followed the controller; it stays as it was."));
        return;
    }
    QString error;
    if (!editDemo(tr("Record the World 1 demo"), steps, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    int frames = 0;
    for (const QJsonValue& step : steps) frames += step.toObject().value(QStringLiteral("frames")).toInt();
    report(sdk::Severity::Info, QStringLiteral("demo"),
           tr("Recorded %1 steps (%2 s) into World 1's demo.").arg(steps.size()).arg(frames / 60.0, 0, 'f', 1));
}

// ---------------------------------------------------------------- playtest

void DoraemonSession::launchWorld(int world) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
    plan.label = tr("Plan playtest from power-on");
    plan.title = tr("Doraemon · power-on");
    if (world > 0) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("doraemon-playtest-plan"), project_.path(),
                          QStringLiteral("--world"), QString::number(world)};
        if (world == 1 && startBlock_.x() >= 0) {
            plan.arguments << (startUnderground_ ? QStringLiteral("--underground") : QStringLiteral("--block"))
                           << QString::number(startBlock_.x()) << QString::number(startBlock_.y());
        } else if (world == 2 && startStage_ >= 0) {
            plan.arguments << QStringLiteral("--stage") << QString::number(startStage_);
        } else if (world == 3 && startRoom_ > 0) {
            plan.arguments << QStringLiteral("--room") << QString::number(startRoom_);
        }
        plan.label = tr("Plan playtest from %1").arg(selectedStart());
        plan.title = tr("Doraemon · %1").arg(selectedStart());
        // The Playtest view covers the boot, the title and the status screen.
        plan.readyFile = QStringLiteral("build/playtest-ready.txt");
        plan.readyVariable = QStringLiteral("DORAEMON_PLAYTEST_READY");
    }
    plan.prepare = [](sdk::ProcessSpec& game) {
        for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, "nes");
    };
    launchPlanned(plan);
}

}  // namespace rl::doraemon
