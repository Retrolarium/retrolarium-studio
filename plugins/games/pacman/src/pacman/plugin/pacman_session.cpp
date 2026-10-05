#include "pacman/plugin/pacman_session.hpp"

#include "nes/palette.hpp"
#include "pacman/plugin/animation_model.hpp"
#include "pacman/plugin/demo_model.hpp"
#include "pacman/plugin/maze_model.hpp"
#include "pacman/plugin/playtest_model.hpp"
#include "pacman/plugin/screen_model.hpp"
#include "pacman/plugin/sound_model.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>
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

// The fruit of rounds 1-16 (tbl_fruit_sprite_tile_by_stage via the stage
// profile); later rounds keep the key.
const int kFruitOfRound[16] = {0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7};

QString tabDocument(const sdk::ProjectFile& project, const QString& codec) {
    for (const QVariant& value : project.list(QStringLiteral("workspace.tabs"))) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("codec")).toString() == codec && tab.value(QStringLiteral("enabled"), true).toBool()) {
            return tab.value(QStringLiteral("document")).toString();
        }
    }
    return {};
}

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

QString failureText(const sdk::ProcessResult& result) {
    if (!result.started) return result.error;
    const QStringList lines = QString::fromUtf8(result.standardError).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (!lines.at(i).trimmed().isEmpty()) return lines.at(i).trimmed();
    }
    return QStringLiteral("exit code %1").arg(result.exitCode);
}

sdk::ProcessSpec specFromPlan(const QJsonObject& json) {
    sdk::ProcessSpec game;
    game.program = json.value(QStringLiteral("program")).toString();
    for (const QJsonValue& argument : json.value(QStringLiteral("arguments")).toArray()) game.arguments << argument.toString();
    game.workingDirectory = json.value(QStringLiteral("working_directory")).toString();
    const QJsonObject environment = json.value(QStringLiteral("environment")).toObject();
    for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
        game.environment.insert(it.key(), it.value().toString());
    }
    return game;
}

QJsonObject loadJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw nes::BundleError(QStringLiteral("Cannot read %1.").arg(path).toStdString());
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) throw nes::BundleError(QStringLiteral("%1 is not a JSON object.").arg(path).toStdString());
    return doc.object();
}

void saveJsonObject(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw nes::BundleError(QStringLiteral("Cannot write %1.").arg(path).toStdString());
    file.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
    if (!file.commit()) throw nes::BundleError(QStringLiteral("Cannot write %1.").arg(path).toStdString());
}

QString paletteLabel(const QString& id) {
    if (id == ids::kGameplay) return QStringLiteral("Gameplay");
    if (id == ids::kTitle) return QStringLiteral("Title");
    if (id == kAttract) return QStringLiteral("Character introduction");
    if (id == kIntermission) return QStringLiteral("Intermission");
    if (id == kFrightened) return QStringLiteral("Frightened ghosts");
    if (id == kFruit) return QStringLiteral("Fruit by round");
    return id;
}

// Sound slots shown together: the two-channel tunes.
const QVector<QPair<QString, QVector<int>>>& tunes() {
    static const QVector<QPair<QString, QVector<int>>> kTunes{
        {QStringLiteral("Player ready"), {0, 1}}, {QStringLiteral("Intermission"), {13, 14}}};
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
        for (const QString& id : {ids::kMaze, ids::kChr, ids::kActors, ids::kScreens, ids::kSound, ids::kGameplay, ids::kTitle}) {
            if (!session->state_.bundle.contains(id)) {
                throw nes::BundleError(QStringLiteral("The resource bundle lacks %1.").arg(id).toStdString());
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
        if (error) *error = QStringLiteral("Cannot open the Pac-Man project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile PacmanSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Pac-Man");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    profile.revision = state_.bundle.revision().contains(QLatin1String("v11")) ? QStringLiteral("J 1.1") : QStringLiteral("J 1.0");
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
        group.title = QStringLiteral("Maze");
        group.documents << entry(QStringLiteral("maze"), QStringLiteral("Maze"), QStringLiteral("27 × 22"),
                                 mazeGrid(state_.bundle) != mazeGrid(saved_.bundle));
        groups << group;
    } else if (kind == QLatin1String("graphics")) {
        sdk::DocumentGroup group;
        group.title = ids::kChr;
        for (int half = 0; half < 2; ++half) {
            bool dirty = false;
            for (int t = half * 256; t < half * 256 + 256 && !dirty; ++t) {
                dirty = state_.bundle.tilePixels(ids::kChr, t) != saved_.bundle.tilePixels(ids::kChr, t);
            }
            group.documents << entry(QStringLiteral("%1/%2").arg(ids::kChr).arg(half),
                                     half == 0 ? QStringLiteral("Background") : QStringLiteral("Sprites"),
                                     QStringLiteral("256 tiles"), dirty);
        }
        groups << group;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Palettes");
        for (const QString& id : state_.bundle.resourceIds(kPaletteCodec)) {
            group.documents << entry(id, paletteLabel(id), QStringLiteral("%1 colours").arg(state_.bundle.tableValues(id).size()),
                                     state_.bundle.tableValues(id) != saved_.bundle.tableValues(id));
        }
        groups << group;
    } else if (kind == QLatin1String("animations")) {
        const QJsonObject now = state_.bundle.resource(ids::kActors);
        const QJsonObject saved = saved_.bundle.resource(ids::kActors);
        const auto frameDirty = [&now, &saved](const PacmanFrame& f) {
            for (const char* key : {"tile_quads", "attribute_quads"}) {
                const QString name = (f.alternate ? QStringLiteral("alternate_") : QStringLiteral("standard_")) + QLatin1String(key);
                if (now.value(name).toArray().at(f.index) != saved.value(name).toArray().at(f.index)) return true;
            }
            return false;
        };
        const QVector<PacmanAnimation>& animations = pacmanAnimations();
        for (int i = 0; i < animations.size(); ++i) {
            const PacmanAnimation& a = animations.at(i);
            auto group = std::find_if(groups.begin(), groups.end(), [&a](const sdk::DocumentGroup& g) { return g.title == a.group; });
            if (group == groups.end()) {
                sdk::DocumentGroup g;
                g.title = a.group;
                groups << g;
                group = groups.end() - 1;
            }
            const bool dirty = std::any_of(a.frames.begin(), a.frames.end(), frameDirty);
            group->documents << entry(QStringLiteral("anim:%1").arg(i), a.label,
                                      a.frames.size() > 1 ? QStringLiteral("%1 fr").arg(a.frames.size()) : QString(), dirty);
        }
    } else if (kind == QLatin1String("screens")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Screens");
        const QJsonObject now = state_.bundle.resource(ids::kScreens);
        const QJsonObject saved = saved_.bundle.resource(ids::kScreens);
        for (const QString& id : screens::ids()) {
            group.documents << entry(id, screens::label(id), QStringLiteral("32 × 30"),
                                     screens::cells(now, id) != screens::cells(saved, id));
        }
        groups << group;
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        group.documents << entry(QStringLiteral("text"), QStringLiteral("Game text"),
                                 QStringLiteral("%1 lines").arg(state_.bundle.resource(ids::kScreens).value(QStringLiteral("text_fields")).toArray().size()),
                                 changed(ids::kScreens));
        groups << group;
    } else if (kind == QLatin1String("sound")) {
        const QJsonArray now = state_.bundle.resource(ids::kSound).value(QStringLiteral("slots")).toArray();
        const QJsonArray saved = saved_.bundle.resource(ids::kSound).value(QStringLiteral("slots")).toArray();
        sdk::DocumentGroup music;
        music.title = QStringLiteral("Music");
        QVector<int> inTunes;
        for (int t = 0; t < tunes().size(); ++t) {
            bool dirty = false;
            for (int slot : tunes().at(t).second) {
                dirty = dirty || now.at(slot) != saved.at(slot);
                inTunes << slot;
            }
            music.documents << entry(QStringLiteral("tune:%1").arg(t), tunes().at(t).first, QStringLiteral("2 ch"), dirty);
        }
        sdk::DocumentGroup effects;
        effects.title = QStringLiteral("Sound effects");
        for (int slot = 0; slot < now.size(); ++slot) {
            if (inTunes.contains(slot)) continue;
            effects.documents << entry(QStringLiteral("slot:%1").arg(slot),
                                       now.at(slot).toObject().value(QStringLiteral("label")).toString(),
                                       QStringLiteral("%1 B").arg(PacmanSoundModel::streamBytes(
                                           now.at(slot).toObject().value(QStringLiteral("stream")).toObject())),
                                       now.at(slot) != saved.at(slot));
        }
        groups << music << effects;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Attract mode");
        // Opening it adds the document from the project's own source.
        group.documents << entry(QStringLiteral("demo"), QStringLiteral("Attract demo"),
                                 state_.demo.isEmpty()
                                     ? QStringLiteral("not added yet")
                                     : QStringLiteral("%1 steps").arg(state_.demo.value(QStringLiteral("steps")).toArray().size()),
                                 !state_.demo.isEmpty() && state_.demo != saved_.demo);
        groups << group;
    } else if (kind == QLatin1String("playtest")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Session");
        group.documents << entry(QStringLiteral("playtest"), QStringLiteral("Build and run"),
                                 lastBuild_.isEmpty() ? QString() : QStringLiteral("built"));
        groups << group;
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> PacmanSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("levels") && document == QLatin1String("maze")) {
            return std::make_unique<PacmanMazeModel>(this);
        }
        if (kind == QLatin1String("graphics")) {
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (document.section(QLatin1Char('/'), 0, 0) == ids::kChr && (half == 0 || half == 1)) {
                return std::make_unique<nes::ChrTileModel>(this, ids::kChr, half * 256, 256,
                                                           half == 0 ? QStringLiteral("Background patterns")
                                                                     : QStringLiteral("Sprite patterns"),
                                                           half == 1);
            }
        }
        if (kind == QLatin1String("palettes") && state_.bundle.resourceIds(kPaletteCodec).contains(document)) {
            return std::make_unique<nes::PaletteTableModel>(this, document, paletteLabel(document));
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:"))) {
            const int index = document.mid(5).toInt();
            if (index >= 0 && index < pacmanAnimations().size()) return std::make_unique<PacmanAnimationModel>(this, index);
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
                        this, QVector<int>{slot}, streams.at(slot).toObject().value(QStringLiteral("label")).toString());
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
    undoStack()->push(new sdk::SnapshotCommand<PacmanState>(
        [this](const PacmanState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, text));
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
    try {
        if (state_.bundle != saved_.bundle) state_.bundle.save(bundlePath_);
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

QVector<sdk::Diagnostic> PacmanSession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    const maze::Report report = maze::inspect(mazeGrid(state_.bundle));
    for (const QString& message : report.errors) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("levels/maze");
        d.message = message;
        d.location = QStringLiteral("Maze");
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
    if (demoStart()) return QStringLiteral("Attract demo");
    return QStringLiteral("Round %1").arg(round_);
}

QString PacmanSession::playSelectedLabel() const {
    if (recordingActive_) return QStringLiteral("Stop recording");
    return demoStart() ? QStringLiteral("Play demo") : QString();
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> PacmanSession::previewPalettes(const QString&, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const auto add = [this, &result](const QString& table, int first, int count, const QString& name) {
        if (!state_.bundle.contains(table)) return;
        const QVector<int> values = state_.bundle.tableValues(table);
        for (int p = 0; p < count; ++p) {
            if ((first + p) * 4 + 4 <= values.size()) {
                result << nes::PreviewPalette{name.arg(p), values.mid((first + p) * 4, 4)};
            }
        }
    };
    if (sprites) {
        add(ids::kGameplay, 4, 4, QStringLiteral("Gameplay SPR %1"));
        add(kAttract, 4, 4, QStringLiteral("Introduction SPR %1"));
        add(kIntermission, 0, 4, QStringLiteral("Intermission SPR %1"));
    } else {
        add(ids::kGameplay, 1, 1, QStringLiteral("Maze"));
        add(ids::kGameplay, 0, 4, QStringLiteral("Gameplay BG %1"));
        add(ids::kTitle, 0, 4, QStringLiteral("Title BG %1"));
        add(kAttract, 0, 4, QStringLiteral("Introduction BG %1"));
    }
    return result;
}

QStringList PacmanSession::paletteRowLabels(const QString& table) const {
    if (table == kFrightened) return {QStringLiteral("Ghosts")};
    if (table == kFruit) {
        return {QStringLiteral("1-4"), QStringLiteral("5-8"), QStringLiteral("9-12"), QStringLiteral("13+")};
    }
    if (table == kIntermission) {
        return {QStringLiteral("SPR 0"), QStringLiteral("SPR 1"), QStringLiteral("SPR 2"), QStringLiteral("SPR 3")};
    }
    QStringList labels;
    const int rows = state_.bundle.contains(table) ? state_.bundle.tableValues(table).size() / 4 : 0;
    for (int r = 0; r < rows; ++r) {
        // Who draws with each row in a round (the round init's palette bytes).
        static const char* const kGameplay[8] = {"BG 0", "Maze", "BG 2", "BG 3", "Pac-Man", "Pink", "Cyan", "Orange"};
        labels << (table == ids::kGameplay ? QString::fromLatin1(kGameplay[r])
                                           : (r < 4 ? QStringLiteral("BG %1") : QStringLiteral("SPR %1")).arg(r % 4));
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
        sample(QStringLiteral("Round"), QStringLiteral("The maze as a round starts"), image);
    } else if (table == ids::kTitle || table == kAttract) {
        const QString id = table == ids::kTitle ? QStringLiteral("title") : QStringLiteral("attract");
        sample(screens::label(id), QStringLiteral("The %1 screen").arg(screens::label(id).toLower()), screens::render(this, id));
    } else if (table == kIntermission) {
        const QVector<QVector<int>> sprites = art.spritePalettes(kIntermission);
        QImage image(13 * 18 + 2, 20, QImage::Format_ARGB32);
        image.fill(backdrop);
        QPainter p(&image);
        for (int i = 0; i < 13; ++i) p.drawImage(2 + i * 18, 2, art.frame(actors, true, i, 0, sprites));
        p.end();
        sample(QStringLiteral("Intermission"), QStringLiteral("The intermission's pieces"), image);
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
        sample(QStringLiteral("Frightened"), QStringLiteral("Ghosts after a power pellet, then flashing"), image);
    } else if (table == kFruit) {
        const QVector<int> colours = state_.bundle.tableValues(kFruit);
        QImage image(16 * 18 + 2, 20, QImage::Format_ARGB32);
        image.fill(backdrop);
        QPainter p(&image);
        for (int r = 0; r < 16; ++r) {
            QVector<QVector<int>> sprites = art.spritePalettes(ids::kGameplay);
            sprites[3][1] = colours.value(r, sprites[3][1]);
            p.drawImage(2 + r * 18, 2, art.frame(actors, false, 36 + kFruitOfRound[r], 3, sprites));
        }
        p.end();
        sample(QStringLiteral("Rounds 1-16"), QStringLiteral("Each round's fruit in its colour"), image);
    }
    return result;
}

// ---------------------------------------------------------------- build and play

void PacmanSession::report(sdk::Severity severity, const QString& source, const QString& message) {
    if (!host()) return;
    sdk::Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = message;
    d.location = project_.path().section(QLatin1Char('/'), -1);
    host()->report(d);
}

void PacmanSession::runBuild(std::function<void(bool)> next) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) {
        next(false);
        return;
    }
    const maze::Report checks = maze::inspect(mazeGrid(state_.bundle));
    if (!checks.errors.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("build"), QStringLiteral("The maze does not pass: %1").arg(checks.errors.first()));
        next(false);
        return;
    }
    if (isModified()) {
        QString error;
        if (!save(&error)) {
            report(sdk::Severity::Error, QStringLiteral("save"), error);
            next(false);
            return;
        }
    }
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("build"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("build"), error);
        next(false);
        return;
    }
    spec.label = QStringLiteral("Build ROM");
    h->runProcess(spec, [this, next](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, QStringLiteral("build"), QStringLiteral("Build failed: %1").arg(failureText(result)));
            }
            next(false);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        const QJsonObject artifact = json.value(QStringLiteral("artifact")).toObject();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
            report(sdk::Severity::Error, QStringLiteral("build"), QStringLiteral("The kit did not report a built ROM."));
            next(false);
            return;
        }
        lastBuild_ = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        report(sdk::Severity::Info, QStringLiteral("build"),
               QStringLiteral("Built %1 (%2 bytes).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt()));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void PacmanSession::build() {
    runBuild([](bool) {});
}

void PacmanSession::play() {
    launch(true);
}

void PacmanSession::playSelected() {
    if (recordingActive_) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launch(false);
}

// ---------------------------------------------------------------- demo

bool PacmanSession::editDemo(const QString& text, const QJsonArray& steps, QString* error) {
    const int pairs = state_.demo.value(QStringLiteral("layout")).toObject().value(QStringLiteral("pairs")).toInt(28);
    if (state_.demo.isEmpty() || steps.isEmpty() || steps.size() > pairs) {
        if (error) *error = QStringLiteral("The demo holds 1-%1 steps.").arg(pairs);
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
    if (!recordingActive_) launchDemo(record);
}

void PacmanSession::stopRecording() {
    if (recordingActive_ && host()) host()->stopEmulator();
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

void PacmanSession::upgradeDocuments(const QString& kind, const QString& document) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) return;
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("pacman-upgrade"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, kind, error);
        return;
    }
    spec.label = QStringLiteral("Add the attract demo");
    h->runProcess(spec, [this, kind, document](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, kind, QStringLiteral("Adding the attract demo failed: %1").arg(failureText(result)));
            }
            return;
        }
        QString loadError;
        const sdk::ProjectFile reloaded = sdk::ProjectFile::load(project_.path(), &loadError);
        if (reloaded.path().isEmpty()) {
            report(sdk::Severity::Error, kind, loadError);
            return;
        }
        project_ = reloaded;
        try {
            // The new document joins the saved state; other unsaved edits stay.
            const QString demo = tabDocument(project_, QStringLiteral("pacman.demo@1"));
            if (!demo.isEmpty() && state_.demo.isEmpty()) {
                demoPath_ = project_.resolve(demo);
                state_.demo = saved_.demo = loadJsonObject(demoPath_);
            }
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
            return;
        }
        report(sdk::Severity::Info, kind, QStringLiteral("Added the attract demo to the project."));
        emit contentChanged();
        for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
        emit documentRequested(kind, document);
    });
}

void PacmanSession::launchDemo(bool record) {
    runBuild([this, record](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) return;
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   QStringLiteral("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QStringList arguments{QStringLiteral("studio"), QStringLiteral("pacman-demo-plan"), project_.path(),
                              QStringLiteral("--mame"), mame};
        const QString recording = project_.resolve(QStringLiteral("build/demo-recording.txt"));
        if (record) {
            QFile::remove(recording);
            arguments << QStringLiteral("--record") << recording;
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        plan.label = record ? QStringLiteral("Plan demo recording") : QStringLiteral("Plan attract demo");
        h->runProcess(plan, [this, record, recording](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) return;
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           QStringLiteral("Demo plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), QStringLiteral("The kit returned no emulator command."));
                return;
            }
            const std::string model = state_.bundle.paletteModel(ids::kGameplay).toStdString();
            for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, model);
            // The Playtest view covers the title and the character introduction.
            game.readyFile = project_.resolve(QStringLiteral("build/demo-ready.txt"));
            QFile::remove(game.readyFile);
            game.environment.insert(QStringLiteral("PACMAN_DEMO_READY"), game.readyFile);
            if (record) {
                recordingActive_ = true;
                emit selectedStartChanged();
                report(sdk::Severity::Info, QStringLiteral("demo"),
                       QStringLiteral("Recording: Pac-Man runs the first step's way for 255 frames, then follows the "
                                      "controller until he is caught, you press Start or Stop recording, or the "
                                      "demo's steps are full."));
            }
            const QString title = record ? QStringLiteral("Pac-Man · recording the demo") : QStringLiteral("Pac-Man · attract demo");
            host->launchEmulator(game, title, [this, record, recording](const sdk::ProcessResult& run) {
                if (run.crashed || (!run.started && !run.error.isEmpty())) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           run.error.isEmpty() ? QStringLiteral("The emulator crashed.") : run.error);
                }
                if (record) {
                    recordingActive_ = false;
                    emit selectedStartChanged();
                    applyRecording(recording);
                }
            });
        });
    });
}

void PacmanSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), QStringLiteral("Nothing was recorded: the demo did not start."));
        return;
    }
    const QJsonArray steps =
        stepsFromRecording(state_.demo.value(QStringLiteral("steps")).toArray(), QString::fromUtf8(file.readAll()));
    if (steps.isEmpty()) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               QStringLiteral("The demo ended before Pac-Man followed the controller; it stays as it was."));
        return;
    }
    QString error;
    if (!editDemo(QStringLiteral("Record the attract demo"), steps, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    int frames = 0;
    for (const QJsonValue& step : steps) frames += step.toObject().value(QStringLiteral("frames")).toInt();
    report(sdk::Severity::Info, QStringLiteral("demo"),
           QStringLiteral("Recorded %1 steps (%2 s of movement) into the attract demo.")
               .arg(steps.size())
               .arg(frames / 60.0, 0, 'f', 1));
}

void PacmanSession::launch(bool fromStart) {
    const int round = round_;
    runBuild([this, fromStart, round](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) return;
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   QStringLiteral("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QStringList arguments;
        if (fromStart) {
            arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path(),
                                    QStringLiteral("--mame"), mame};
        } else {
            arguments = QStringList{QStringLiteral("studio"), QStringLiteral("pacman-round-playtest-plan"), project_.path(),
                                    QStringLiteral("--mame"), mame, QStringLiteral("--round"), QString::number(round)};
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        const QString title = fromStart ? QStringLiteral("Pac-Man · power-on") : QStringLiteral("Pac-Man · Round %1").arg(round);
        plan.label = fromStart ? QStringLiteral("Plan playtest from power-on")
                               : QStringLiteral("Plan playtest of round %1").arg(round);
        h->runProcess(plan, [this, title, fromStart](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) return;
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           QStringLiteral("Playtest plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), QStringLiteral("The kit returned no emulator command."));
                return;
            }
            const std::string model = state_.bundle.paletteModel(ids::kGameplay).toStdString();
            for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, model);
            if (!fromStart) {
                // The Playtest view covers the title and the way into the round.
                game.readyFile = project_.resolve(QStringLiteral("build/playtest-ready.txt"));
                QFile::remove(game.readyFile);
                game.environment.insert(QStringLiteral("PACMAN_PLAYTEST_READY"), game.readyFile);
            }
            host->launchEmulator(game, title, [this](const sdk::ProcessResult& run) {
                if (run.crashed || (!run.started && !run.error.isEmpty())) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           run.error.isEmpty() ? QStringLiteral("The emulator crashed.") : run.error);
                }
            });
        });
    });
}

}  // namespace rl::pacman
