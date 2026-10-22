#include "pacman/plugin/pacman_session.hpp"

#include "nes/palette.hpp"
#include "nes/record_table_model.hpp"
#include "pacman/plugin/animation_model.hpp"
#include "pacman/plugin/demo_model.hpp"
#include "pacman/plugin/maze_model.hpp"
#include "pacman/plugin/playtest_model.hpp"
#include "pacman/plugin/screen_model.hpp"
#include "pacman/plugin/sound_model.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QUndoStack>

#include <algorithm>

namespace rl::pacman {

namespace {

const QString kPaletteCodec = QStringLiteral("pacman.palette-table.v1");
const QString kChrCodec = QStringLiteral("nes.chr-2bpp.v1");
const QString kAttract = QStringLiteral("attract-bg-sprite");
const QString kIntermission = QStringLiteral("intermission-sprites");
const QString kFrightened = QStringLiteral("frightened-active-normal");
const QString kFruit = QStringLiteral("fruit-by-stage");
const QString kRoundProfiles = QStringLiteral("round-profiles");
const QString kRenderTables = QStringLiteral("render-tables");

using sdk::loadJsonObject;
using sdk::saveJsonObject;
using sdk::tabDocument;

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

QString paletteLabel(const QString& id) {
    if (id == ids::kGameplay) return rlTr("pacman", "Gameplay");
    if (id == ids::kTitle) return rlTr("pacman", "Title");
    if (id == kAttract) return rlTr("pacman", "Character introduction");
    if (id == kIntermission) return QStringLiteral("Intermission");
    if (id == kFrightened) return rlTr("pacman", "Frightened ghosts");
    if (id == kFruit) return rlTr("pacman", "Fruit by round");
    return id;
}

// The round tables (kit nes.record-table.v1), as the Levels view lists them.
struct RoundTable {
    const char* id;
    const char* title;
    const char* note;
};
const RoundTable kRoundTables[] = {
    {"round-profiles", QT_TRANSLATE_NOOP("pacman", "Rounds"),
     QT_TRANSLATE_NOOP("pacman",
                       "Per round: the speed and timing set, how long ghosts stay frightened, the "
                       "pellet-threshold and release-target sets, the fruit group and the seconds "
                       "between ghost releases.")},
    {"round-speed-sets", QT_TRANSLATE_NOOP("pacman", "Speed sets"),
     QT_TRANSLATE_NOOP("pacman",
                       "The 22 bytes a speed and timing set copies to the level parameters ($009F-$00B4): "
                       "the fixed-point speeds of Pac-Man and the ghosts.")},
    {"round-timing-sets", QT_TRANSLATE_NOOP("pacman", "Scatter and chase timing"),
     QT_TRANSLATE_NOOP("pacman",
                       "The eight scatter and chase durations of sets 0-3. Set 4 reads the first four "
                       "pellet-threshold pairs instead, as the original does.")},
    {"round-pellet-thresholds", QT_TRANSLATE_NOOP("pacman", "Pellet thresholds"),
     QT_TRANSLATE_NOOP("pacman", "The pellet counts that let ghosts out of the house, a pair per set.")},
    {"round-release-targets", QT_TRANSLATE_NOOP("pacman", "Release targets"),
     QT_TRANSLATE_NOOP("pacman",
                       "How many ghosts leave the house at once and the pellet target after them. A "
                       "restarted round uses Restart, less the pellets already eaten.")},
};

// Sound slots shown together: the two-channel tunes.
const QVector<QPair<QString, QVector<int>>>& tunes() {
    static const QVector<QPair<QString, QVector<int>>> kTunes{{rlTr("pacman", "Player ready"), {0, 1}},
                                                              {QStringLiteral("Intermission"), {13, 14}}};
    return kTunes;
}

}  // namespace

// ---------------------------------------------------------------- opening

std::unique_ptr<PacmanSession> PacmanSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<PacmanSession> session(new PacmanSession);
    session->project_ = project;
    try {
        const QString document = tabDocument(project, QStringLiteral("pacman.maze-rle.v1"));
        if (document.isEmpty()) throw nes::BundleError("The project has no Pac-Man maze tab.");
        session->bundlePath_ = project.resolve(document);
        session->state_.bundle = nes::Bundle::load(session->bundlePath_);
        for (const QString& id :
             {ids::kMaze, ids::kChr, ids::kActors, ids::kScreens, ids::kSound, ids::kGameplay, ids::kTitle}) {
            if (!session->state_.bundle.contains(id)) {
                throw nes::BundleError(tr("The resource bundle lacks %1.").arg(id).toStdString());
            }
        }
        if (session->mazeGrid(session->state_.bundle).size() != maze::kRows * maze::kColumns) {
            throw nes::BundleError("The maze is not 27x22.");
        }
        const QString demo = tabDocument(project, QStringLiteral("pacman.demo@1"));
        if (!demo.isEmpty()) {
            session->demoPath_ = project.resolve(demo);
            session->state_.demo = loadJsonObject(session->demoPath_);
        }
        session->saved_ = session->state_;
        session->painter();
    } catch (const std::exception& e) {
        if (error) *error = tr("Cannot open the Pac-Man project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile PacmanSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Pac-Man");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    const QString revision = state_.bundle.revision();
    profile.revision = revision.contains(QLatin1String("nrom256")) ? QStringLiteral("J 1.0 NROM-256")
                       : revision.contains(QLatin1String("v11"))   ? QStringLiteral("J 1.1")
                                                                   : QStringLiteral("J 1.0");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("pacman.maze-rle.v1");
    return profile;
}

QVector<sdk::Kind> PacmanSession::kinds() const {
    return {kind("levels", "Levels", "levels", sdk::EditorType::TileMap),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("animations", "Animations", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sound", "Sound", "sound", sdk::EditorType::Sequencer),
            kind("demo", "Demo", "play", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> PacmanSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    const auto changed = [this](const QString& id) {
        return state_.bundle.contains(id) && state_.bundle.resource(id) != saved_.bundle.resource(id);
    };
    if (kind == QLatin1String("levels")) {
        sdk::DocumentGroup group;
        group.title = tr("Maze");
        group.documents << entry(QStringLiteral("maze"), tr("Maze"), QStringLiteral("27 × 22"),
                                 mazeGrid(state_.bundle) != mazeGrid(saved_.bundle));
        groups << group;
        // Projects prepared by an older kit lack them until it refreshes them.
        sdk::DocumentGroup rounds;
        rounds.title = tr("Rounds");
        for (const RoundTable& table : kRoundTables) {
            const QString id = QString::fromLatin1(table.id);
            if (!state_.bundle.contains(id)) continue;
            rounds.documents << entry(
                QStringLiteral("table:") + id, rlTr("pacman", table.title),
                tr("%n rows", nullptr,
                   int(state_.bundle.resource(id).value(QStringLiteral("records")).toArray().size())),
                saved_.bundle.contains(id) && changed(id));
        }
        if (!rounds.documents.isEmpty()) groups << rounds;
    } else if (kind == QLatin1String("graphics")) {
        sdk::DocumentGroup group;
        group.title = ids::kChr;
        for (int half = 0; half < 2; ++half) {
            bool dirty = false;
            for (int t = half * 256; t < half * 256 + 256 && !dirty; ++t) {
                dirty = state_.bundle.tilePixels(ids::kChr, t) != saved_.bundle.tilePixels(ids::kChr, t);
            }
            group.documents << entry(QStringLiteral("%1/%2").arg(ids::kChr).arg(half),
                                     half == 0 ? tr("Background") : tr("Sprites"), tr("256 tiles"), dirty);
        }
        groups << group;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup group;
        group.title = tr("Palettes");
        for (const QString& id : state_.bundle.resourceIds(kPaletteCodec)) {
            group.documents << entry(id, paletteLabel(id),
                                     tr("%n colours", nullptr, state_.bundle.tableValues(id).size()),
                                     state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
        }
        groups << group;
    } else if (kind == QLatin1String("animations")) {
        const QJsonObject now = state_.bundle.resource(ids::kActors);
        const QJsonObject saved = saved_.bundle.resource(ids::kActors);
        const auto frameDirty = [&now, &saved](const PacmanFrame& f) {
            for (const char* key : {"tile_quads", "attribute_quads"}) {
                const QString name =
                    (f.alternate ? QStringLiteral("alternate_") : QStringLiteral("standard_")) + QLatin1String(key);
                if (now.value(name).toArray().at(f.index) != saved.value(name).toArray().at(f.index)) return true;
            }
            return false;
        };
        const QVector<PacmanAnimation>& animations = pacmanAnimations();
        for (int i = 0; i < animations.size(); ++i) {
            const PacmanAnimation& a = animations.at(i);
            auto group = std::find_if(groups.begin(), groups.end(),
                                      [&a](const sdk::DocumentGroup& g) { return g.title == a.group; });
            if (group == groups.end()) {
                sdk::DocumentGroup g;
                g.title = a.group;
                groups << g;
                group = groups.end() - 1;
            }
            const bool dirty = std::any_of(a.frames.begin(), a.frames.end(), frameDirty);
            group->documents << entry(QStringLiteral("anim:%1").arg(i), animationLabel(a),
                                      a.frames.size() > 1 ? tr("%1 fr").arg(a.frames.size()) : QString(), dirty);
        }
    } else if (kind == QLatin1String("screens")) {
        sdk::DocumentGroup group;
        group.title = tr("Screens");
        const QJsonObject now = state_.bundle.resource(ids::kScreens);
        const QJsonObject saved = saved_.bundle.resource(ids::kScreens);
        for (const QString& id : screens::ids()) {
            group.documents << entry(
                id, screens::label(id), QStringLiteral("32 × 30"),
                screens::cells(now, id, attractAttributes()) != screens::cells(saved, id, attractAttributes()));
        }
        groups << group;
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        group.documents << entry(
            QStringLiteral("text"), tr("Game text"),
            tr("%n lines", nullptr,
               state_.bundle.resource(ids::kScreens).value(QStringLiteral("text_fields")).toArray().size()),
            changed(ids::kScreens));
        groups << group;
    } else if (kind == QLatin1String("sound")) {
        const QJsonArray now = state_.bundle.resource(ids::kSound).value(QStringLiteral("slots")).toArray();
        const QJsonArray saved = saved_.bundle.resource(ids::kSound).value(QStringLiteral("slots")).toArray();
        sdk::DocumentGroup music;
        music.title = tr("Music");
        QVector<int> inTunes;
        for (int t = 0; t < tunes().size(); ++t) {
            bool dirty = false;
            for (int slot : tunes().at(t).second) {
                dirty = dirty || now.at(slot) != saved.at(slot);
                inTunes << slot;
            }
            music.documents << entry(QStringLiteral("tune:%1").arg(t), tunes().at(t).first, tr("2 ch"), dirty);
        }
        sdk::DocumentGroup effects;
        effects.title = tr("Sound effects");
        for (int slot = 0; slot < now.size(); ++slot) {
            if (inTunes.contains(slot)) continue;
            effects.documents << entry(
                QStringLiteral("slot:%1").arg(slot), now.at(slot).toObject().value(QStringLiteral("label")).toString(),
                QStringLiteral("%1 B").arg(
                    PacmanSoundModel::streamBytes(now.at(slot).toObject().value(QStringLiteral("stream")).toObject())),
                now.at(slot) != saved.at(slot));
        }
        groups << music << effects;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup group;
        group.title = tr("Attract mode");
        // Opening it adds the document from the project's own source.
        group.documents << entry(
            QStringLiteral("demo"), tr("Attract demo"),
            state_.demo.isEmpty()
                ? tr("not added yet")
                : tr("%n steps", nullptr, state_.demo.value(QStringLiteral("steps")).toArray().size()),
            !state_.demo.isEmpty() && state_.demo != saved_.demo);
        groups << group;
    } else if (kind == QLatin1String("playtest")) {
        sdk::DocumentGroup group;
        group.title = tr("Session");
        group.documents << entry(QStringLiteral("playtest"), tr("Build and run"),
                                 lastBuild().isEmpty() ? QString() : QStringLiteral("built"));
        groups << group;
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> PacmanSession::openEditor(const QString& kind, const QString& document) {
    try {
        // Screens and palettes draw with data a newer kit reads from the
        // game (the attract attributes, the round table's fruit).
        if ((kind == QLatin1String("screens") || kind == QLatin1String("palettes")) && !hasGameTables()) {
            upgradeDocuments(kind, document);
            return nullptr;
        }
        if (kind == QLatin1String("levels") && document == QLatin1String("maze")) {
            return std::make_unique<PacmanMazeModel>(this);
        }
        if (kind == QLatin1String("levels") && document.startsWith(QLatin1String("table:"))) {
            const QString id = document.mid(6);
            for (const RoundTable& table : kRoundTables) {
                if (id == QLatin1String(table.id) && state_.bundle.contains(id)) {
                    return std::make_unique<nes::RecordTableModel>(this, id, rlTr("pacman", table.title),
                                                                   rlTr("pacman", table.note));
                }
            }
            return nullptr;
        }
        if (kind == QLatin1String("graphics")) {
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (document.section(QLatin1Char('/'), 0, 0) == ids::kChr && (half == 0 || half == 1)) {
                return std::make_unique<nes::ChrTileModel>(
                    this, ids::kChr, half * 256, 256, half == 0 ? tr("Background patterns") : tr("Sprite patterns"),
                    half == 1);
            }
        }
        if (kind == QLatin1String("palettes") && state_.bundle.resourceIds(kPaletteCodec).contains(document)) {
            return std::make_unique<nes::PaletteTableModel>(this, document, paletteLabel(document));
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:"))) {
            const int index = document.mid(5).toInt();
            if (index >= 0 && index < pacmanAnimations().size())
                return std::make_unique<PacmanAnimationModel>(this, index);
        }
        if (kind == QLatin1String("screens") && screens::ids().contains(document)) {
            return std::make_unique<PacmanScreenModel>(this, document);
        }
        if (kind == QLatin1String("text") && document == QLatin1String("text")) {
            return std::make_unique<PacmanTextModel>(this);
        }
        if (kind == QLatin1String("sound")) {
            if (document.startsWith(QLatin1String("tune:"))) {
                const int t = document.mid(5).toInt();
                if (t >= 0 && t < tunes().size()) {
                    return std::make_unique<PacmanSoundModel>(this, tunes().at(t).second, tunes().at(t).first);
                }
            }
            if (document.startsWith(QLatin1String("slot:"))) {
                const int slot = document.mid(5).toInt();
                const QJsonArray streams = state_.bundle.resource(ids::kSound).value(QStringLiteral("slots")).toArray();
                if (slot >= 0 && slot < streams.size()) {
                    return std::make_unique<PacmanSoundModel>(
                        this, QVector<int>{slot},
                        streams.at(slot).toObject().value(QStringLiteral("label")).toString());
                }
            }
        }
        if (kind == QLatin1String("demo") && document == QLatin1String("demo")) {
            if (state_.demo.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            return std::make_unique<PacmanDemoModel>(this);
        }
        if (kind == QLatin1String("playtest")) return std::make_unique<PacmanPlaytestModel>(this);
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const PacmanPainter& PacmanSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<PacmanPainter>(state_.bundle);
        painterRevision_ = revision_;
    }
    return *painter_;
}

QVector<int> PacmanSession::mazeGrid(const nes::Bundle& bundle) const {
    QVector<int> grid;
    if (!bundle.contains(ids::kMaze)) return grid;
    for (const QJsonValue& row : bundle.resource(ids::kMaze).value(QStringLiteral("tiles")).toArray()) {
        for (const QJsonValue& tile : row.toArray()) grid << tile.toInt();
    }
    return grid;
}

void PacmanSession::setState(const PacmanState& state, quint64 revision) {
    const QJsonObject demo = state_.demo;
    state_ = state;
    // Snapshots from before the kit added the demo keep it.
    if (state_.demo.isEmpty()) state_.demo = demo;
    revision_ = revision;
    emit contentChanged();
    emit diagnosticsChanged();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
}

bool PacmanSession::edit(const QString& text, const PacmanState& next, QString*) {
    if (next == state_) return true;
    undoStack()->push(
        new sdk::SnapshotCommand<PacmanState>([this](const PacmanState& s, quint64 revision) { setState(s, revision); },
                                              state_, revision_, next, nextRevision_++, text));
    return true;
}

bool PacmanSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) {
    PacmanState next = state_;
    try {
        change(next.bundle);
        // The PPU mirrors the sprite backdrop entries onto the background's:
        // an edit to one moves its pair.
        for (const QString& id : next.bundle.resourceIds(kPaletteCodec)) {
            const QVector<int> before = state_.bundle.tableValues(id);
            const QVector<int> after = next.bundle.tableValues(id);
            for (const QJsonValue& pair : next.bundle.resource(id).value(QStringLiteral("mirror_pairs")).toArray()) {
                const int a = pair.toArray().at(0).toInt();
                const int b = pair.toArray().at(1).toInt();
                if (a >= after.size() || b >= after.size()) continue;
                if (after.at(a) != before.value(a)) next.bundle.setTableValue(id, b, after.at(a));
                else if (after.at(b) != before.value(b)) next.bundle.setTableValue(id, a, after.at(b));
            }
        }
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return edit(text, next, error);
}

bool PacmanSession::save(QString* error) {
    // Each document counts as saved once written: a save that fails
    // halfway leaves the ones before it saved.
    try {
        if (state_.bundle != saved_.bundle) {
            state_.bundle.save(bundlePath_);
            saved_.bundle = state_.bundle;
        }
        if (state_.demo != saved_.demo && !demoPath_.isEmpty()) {
            saveJsonObject(demoPath_, state_.demo);
            saved_.demo = state_.demo;
        }
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    saved_ = state_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    return true;
}

QVector<sdk::Diagnostic> PacmanSession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    const maze::Report report = maze::inspect(mazeGrid(state_.bundle));
    for (const QString& message : report.errors) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("levels/maze");
        d.message = message;
        d.location = tr("Maze");
        d.kind = QStringLiteral("levels");
        d.document = QStringLiteral("maze");
        result << d;
    }
    return result;
}

void PacmanSession::setStartRound(int round) {
    const int next = std::clamp(round, 1, 23);
    if (next == round_) return;
    round_ = next;
    emit startChanged();
    emit selectedStartChanged();
    emit contentChanged();
}

QString PacmanSession::selectedStart() const {
    if (demoStart()) return tr("Attract demo");
    return tr("Round %1").arg(round_);
}

QString PacmanSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> PacmanSession::previewPalettes(const QString&, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const auto add = [this, &result](const QString& table, int first, int count, const QString& name) {
        if (!state_.bundle.contains(table)) return;
        const QVector<int> values = state_.bundle.tableValues(table);
        for (int p = 0; p < count; ++p) {
            if ((first + p) * 4 + 4 <= values.size()) {
                result << nes::PreviewPalette{count == 1 ? name : name.arg(p), values.mid((first + p) * 4, 4)};
            }
        }
    };
    if (sprites) {
        add(ids::kGameplay, 4, 4, tr("Gameplay SPR %1"));
        add(kAttract, 4, 4, tr("Introduction SPR %1"));
        add(kIntermission, 0, 4, tr("Intermission SPR %1"));
    } else {
        add(ids::kGameplay, 1, 1, tr("Maze"));
        add(ids::kGameplay, 0, 4, tr("Gameplay BG %1"));
        add(ids::kTitle, 0, 4, tr("Title BG %1"));
        add(kAttract, 0, 4, tr("Introduction BG %1"));
    }
    return result;
}

QStringList PacmanSession::paletteRowLabels(const QString& table) const {
    if (table == kFrightened) return {QStringLiteral("Ghosts")};
    if (table == kFruit) {
        return {QStringLiteral("1-4"), QStringLiteral("5-8"), QStringLiteral("9-12"), QStringLiteral("13+")};
    }
    if (table == kIntermission) {
        return {tr("SPR 0"), tr("SPR 1"), tr("SPR 2"), tr("SPR 3")};
    }
    QStringList labels;
    const int rows = state_.bundle.contains(table) ? state_.bundle.tableValues(table).size() / 4 : 0;
    for (int r = 0; r < rows; ++r) {
        // Who draws with each row in a round (the round init's palette bytes).
        static const char* const kGameplay[5] = {"BG 0", "Maze", "BG 2", "BG 3", "Pac-Man"};
        const QString gameplay = r < 5 ? QString::fromLatin1(kGameplay[r]) : ghostName(r - 4);
        labels << (table == ids::kGameplay ? gameplay : (r < 4 ? tr("BG %1") : tr("SPR %1")).arg(r % 4));
    }
    return labels;
}

QVector<sdk::PaletteSample> PacmanSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    const PacmanPainter& art = painter();
    const QJsonObject actors = state_.bundle.resource(ids::kActors);
    const QColor backdrop = art.color(state_.bundle.tableValues(ids::kGameplay).value(0, 0x0f));
    const auto sample = [&result](const QString& label, const QString& caption, const QImage& image) {
        sdk::PaletteSample s;
        s.label = label;
        s.caption = caption;
        s.image = image;
        result << s;
    };
    if (table == ids::kGameplay) {
        // The maze with everyone where a round starts.
        QImage image = art.maze(mazeGrid(state_.bundle));
        QPainter p(&image);
        p.drawImage(maze::kPacmanColumn * 8 - 4, maze::kPacmanRow * 8 - 4, art.frame(actors, false, 4, 0));
        const int ghosts[4][3] = {{10, 11, 12}, {13, 11, 14}, {13, 9, 10}, {13, 13, 10}};
        for (int g = 0; g < 4; ++g) {
            p.drawImage(ghosts[g][1] * 8 - 4, ghosts[g][0] * 8 - 4, art.frame(actors, false, ghosts[g][2], g));
        }
        p.end();
        sample(tr("Round"), tr("The maze as a round starts"), image);
    } else if (table == ids::kTitle || table == kAttract) {
        const QString id = table == ids::kTitle ? QStringLiteral("title") : QStringLiteral("attract");
        sample(screens::label(id), tr("The %1 screen").arg(screens::label(id).toLower()), screens::render(this, id));
    } else if (table == kIntermission) {
        const QVector<QVector<int>> sprites = art.spritePalettes(kIntermission);
        QImage image(13 * 18 + 2, 20, QImage::Format_ARGB32);
        image.fill(backdrop);
        QPainter p(&image);
        for (int i = 0; i < 13; ++i) p.drawImage(2 + i * 18, 2, art.frame(actors, true, i, 0, sprites));
        p.end();
        sample(QStringLiteral("Intermission"), tr("The intermission's pieces"), image);
    } else if (table == kFrightened) {
        // Frightened ghosts draw with sprite palette 1; the game writes the
        // first colour into its entry 1 ($3F15), the second while they flash.
        const QVector<int> values = state_.bundle.tableValues(kFrightened);
        QImage image(4 * 18 + 2, 20, QImage::Format_ARGB32);
        image.fill(backdrop);
        QPainter p(&image);
        for (int g = 0; g < 4; ++g) {
            QVector<int> palette = art.spritePalettes(ids::kGameplay).at(1);
            palette[1] = values.value(g < 2 ? 0 : 1, 0x11);
            const QVector<QVector<int>> sprites{palette, palette, palette, palette};
            p.drawImage(2 + g * 18, 2, art.frame(actors, false, 30 + g % 2, 1, sprites));
        }
        p.end();
        sample(tr("Frightened"), tr("Ghosts after a power pellet, then flashing"), image);
    } else if (table == kFruit) {
        const QVector<int> colours = state_.bundle.tableValues(kFruit);
        QImage image(16 * 18 + 2, 20, QImage::Format_ARGB32);
        image.fill(backdrop);
        QPainter p(&image);
        // The fruit of each round, from the round table.
        const QJsonArray rounds =
            state_.bundle.contains(kRoundProfiles)
                ? state_.bundle.resource(kRoundProfiles).value(QStringLiteral("records")).toArray()
                : QJsonArray();
        for (int r = 0; r < 16 && r < rounds.size(); ++r) {
            QVector<QVector<int>> sprites = art.spritePalettes(ids::kGameplay);
            sprites[3][1] = colours.value(r, sprites[3][1]);
            const int fruit = rounds.at(r).toObject().value(QStringLiteral("fruit_group")).toInt();
            p.drawImage(2 + r * 18, 2, art.frame(actors, false, 36 + fruit, 3, sprites));
        }
        p.end();
        sample(tr("Rounds 1-16"), tr("Each round's fruit in its colour"), image);
    }
    return result;
}

// ---------------------------------------------------------------- build and play

bool PacmanSession::prepareBuild(BuildStep&, QString* error) {
    const maze::Report checks = maze::inspect(mazeGrid(state_.bundle));
    if (!checks.errors.isEmpty()) {
        if (error) *error = tr("The maze does not pass: %1").arg(checks.errors.first());
        return false;
    }
    return true;
}

void PacmanSession::build() {
    runBuild([](bool) {});
}

void PacmanSession::play() { launch(true); }

void PacmanSession::playSelected() {
    if (isRecording()) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launch(false);
}

// ---------------------------------------------------------------- demo

bool PacmanSession::editDemo(const QString& text, const QJsonArray& steps, QString* error) {
    const int pairs = state_.demo.value(QStringLiteral("layout")).toObject().value(QStringLiteral("pairs")).toInt(28);
    if (state_.demo.isEmpty() || steps.isEmpty() || steps.size() > pairs) {
        if (error) *error = tr("The demo holds 1-%1 steps.").arg(pairs);
        return false;
    }
    PacmanState next = state_;
    next.demo.insert(QStringLiteral("steps"), steps);
    return edit(text, next, error);
}

void PacmanSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) {
        emit startChanged();
        emit selectedStartChanged();
    }
}

void PacmanSession::playDemo(bool record) {
    if (!isRecording()) launchDemo(record);
}

QJsonArray PacmanSession::stepsFromRecording(const QJsonArray& steps, const QString& text) {
    static const QStringList kDirections{QStringLiteral("up"), QStringLiteral("left"), QStringLiteral("down"),
                                         QStringLiteral("right")};
    QJsonArray result;
    if (steps.isEmpty()) return result;
    result.append(steps.first());
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList words = line.simplified().split(QLatin1Char(' '));
        if (words.size() != 2) continue;
        const int direction = words.at(0).toInt();
        const int updates = words.at(1).toInt();
        if (direction < 0 || direction > 3 || updates < 1 || updates > 256) continue;
        result.append(QJsonObject{{QStringLiteral("index"), result.size()},
                                  {QStringLiteral("direction"), kDirections.at(direction)},
                                  {QStringLiteral("frames"), updates}});
    }
    return result.size() > 1 ? result : QJsonArray();
}

QVector<int> PacmanSession::attractAttributes() const {
    QVector<int> table(64, 0);
    if (!state_.bundle.contains(kRenderTables)) return table;
    const QJsonObject tables = state_.bundle.resource(kRenderTables).value(QStringLiteral("tables")).toObject();
    const auto value = [&tables](const char* name) {
        return tables.value(QLatin1String(name)).toArray().at(0).toInt();
    };
    // Rows of equal bytes from $23D0, each row the step above the last,
    // then three footer bytes and a last one at $23E8.
    const int rows = value("attract_attribute_rows");
    const int length = value("attract_attribute_row_length");
    int byte = value("attract_attribute_start");
    for (int row = 0; row < rows; ++row) {
        for (int i = 0; i < length && 16 + row * length + i < 64; ++i) table[16 + row * length + i] = byte;
        byte = (byte + value("attract_attribute_step")) & 0xff;
    }
    for (int i = 0; i < 3; ++i) table[40 + i] = value("attract_attribute_footer");
    table[43] = value("attract_attribute_footer_end");
    return table;
}

bool PacmanSession::hasGameTables() const {
    return state_.bundle.contains(kRenderTables) && state_.bundle.contains(kRoundProfiles);
}

void PacmanSession::upgradeDocuments(const QString& kind, const QString& document) {
    UpgradeStep step;
    step.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("pacman-upgrade"), project_.path()};
    step.label = tr("Add what this kit edits to the project");
    step.source = kind;
    step.failure = tr("Upgrading the project failed: %1");
    step.saveFirst = true;
    step.adopt = [this] {
        // The new document joins the saved state; other unsaved edits stay.
        const QString demo = tabDocument(project_, QStringLiteral("pacman.demo@1"));
        if (!demo.isEmpty() && state_.demo.isEmpty()) {
            demoPath_ = project_.resolve(demo);
            state_.demo = saved_.demo = loadJsonObject(demoPath_);
        }
        // Resources the bundle gained, read from the game; part of the
        // project from now on, not an edit to undo.
        const nes::Bundle bundle = nes::Bundle::load(bundlePath_);
        if (bundle != state_.bundle) {
            state_.bundle = saved_.bundle = bundle;
        }
    };
    step.done = tr("Added the documents and resources this kit edits to the project.");
    step.openKind = kind;
    step.openDocument = document;
    runUpgrade(step);
}

void PacmanSession::launchDemo(bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("pacman-demo-plan"), project_.path()};
    plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
    plan.title = record ? tr("Pac-Man · recording the demo") : tr("Pac-Man · attract demo");
    plan.demo = true;
    // The Playtest view covers the title and the character introduction.
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("PACMAN_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage =
            tr("Recording: Pac-Man runs the first step's way for 255 frames, then follows the "
               "controller until he is caught, you press Start or Stop recording, or the "
               "demo's steps are full.");
        plan.recorded = [this](const QString& path) { applyRecording(path); };
    }
    plan.prepare = [this](sdk::ProcessSpec& game) { useColours(game); };
    launchPlanned(plan);
}

void PacmanSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const QJsonArray steps =
        stepsFromRecording(state_.demo.value(QStringLiteral("steps")).toArray(), QString::fromUtf8(file.readAll()));
    if (steps.isEmpty()) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               tr("The demo ended before Pac-Man followed the controller; it stays as it was."));
        return;
    }
    QString error;
    if (!editDemo(tr("Record the attract demo"), steps, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    int frames = 0;
    for (const QJsonValue& step : steps) frames += step.toObject().value(QStringLiteral("frames")).toInt();
    report(sdk::Severity::Info, QStringLiteral("demo"),
           tr("Recorded %1 steps (%2 s of movement) into the attract demo.")
               .arg(steps.size())
               .arg(frames / 60.0, 0, 'f', 1));
}

void PacmanSession::launch(bool fromStart) {
    const int round = round_;
    EmulatorPlan plan;
    if (fromStart) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("Pac-Man · power-on");
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("pacman-round-playtest-plan"),
                                     project_.path(), QStringLiteral("--round"), QString::number(round)};
        plan.label = tr("Plan playtest of round %1").arg(round);
        plan.title = tr("Pac-Man · Round %1").arg(round);
        // The Playtest view covers the title and the way into the round.
        plan.readyFile = QStringLiteral("build/playtest-ready.txt");
        plan.readyVariable = QStringLiteral("PACMAN_PLAYTEST_READY");
    }
    plan.prepare = [this](sdk::ProcessSpec& game) { useColours(game); };
    launchPlanned(plan);
}

void PacmanSession::useColours(sdk::ProcessSpec& game) const {
    // The game in the colours the editors draw with.
    const std::string model = state_.bundle.paletteModel(ids::kGameplay).toStdString();
    for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, model);
}

}  // namespace rl::pacman
