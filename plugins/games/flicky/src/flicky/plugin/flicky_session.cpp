#include "flicky/plugin/flicky_session.hpp"

#include "flicky/plugin/graphics_models.hpp"
#include "flicky/plugin/levels_model.hpp"
#include "flicky/plugin/objects.hpp"
#include "flicky/plugin/semantics_models.hpp"
#include "megadrive/vdp.hpp"

#include "flicky/plugin/bonus_model.hpp"
#include "flicky/plugin/demo_model.hpp"
#include "flicky/plugin/playtest_model.hpp"
#include "flicky/plugin/screen_model.hpp"
#include "flicky/plugin/sound_models.hpp"
#include "flicky/plugin/sprite_models.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/snapshot_command.hpp"
#include "sdk/plugin.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <stdexcept>

namespace rl::flicky {

namespace {

// The document of the project's first enabled tab of `kind`
// ("flicky.levels"): Flicky's tabs name their kind.
QString tabOfKind(const sdk::ProjectFile& project, const QString& kind) {
    for (const QVariant& value : project.list(QStringLiteral("workspace.tabs"))) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("kind")).toString() == kind &&
            tab.value(QStringLiteral("enabled"), true).toBool()) {
            return tab.value(QStringLiteral("document")).toString();
        }
    }
    return {};
}

// One edit: swaps between two whole-document states. Documents keep their
// JSON implicitly shared, so a snapshot costs little until it is changed, and
// every edit, including multi-field ones, is exactly reversible.
using sdk::SnapshotCommand;

bool sameObjects(const FlickyLevelDocument& a, const FlickyLevelDocument& b, int layout) {
    for (const QString& field : FlickyLevelDocument::objectFields()) {
        if (a.objectPositions(layout, field) != b.objectPositions(layout, field)) {
            return false;
        }
    }
    return true;
}

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta,
                         bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

sdk::DocumentGroup group(const QString& title, const QVector<sdk::DocumentEntry>& documents = {}) {
    sdk::DocumentGroup result;
    result.title = title;
    result.documents = documents;
    return result;
}

sdk::Kind kind(const char* id, const char* label, sdk::EditorType editor) {
    sdk::Kind result;
    result.id = QString::fromLatin1(id);
    result.label = QString::fromLatin1(label);
    result.icon = result.id;
    result.editor = editor;
    return result;
}

QString two(int value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

template <class T>
void loadTracked(Tracked<T>& tracked, const QString& path, const QString& sourceRoot) {
    tracked.path = path;
    tracked.doc = std::make_shared<T>(T::load(path));
    // A game repository keeps the documents as first extracted in original/
    // beside flicky-source/: Revert goes back to those, whatever was saved
    // since. Without them, to the documents as opened.
    const QString pristine = QDir(sourceRoot).absoluteFilePath(
        QStringLiteral("../original/") + QDir(sourceRoot).relativeFilePath(path));
    if (QFileInfo::exists(pristine)) {
        try {
            tracked.doc->adoptBaseline(T::load(pristine));
        } catch (const std::exception&) {
        }
    }
    tracked.saved = *tracked.doc;
}

bool sameText(const FlickySemanticsDocument& a, const FlickySemanticsDocument& b) {
    for (const QString& id : a.textIds()) {
        if (a.textValue(id) != b.textValue(id)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------- opening

std::unique_ptr<FlickySession> FlickySession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<FlickySession> session(new FlickySession);
    session->project_ = project;
    const auto require = [&](const QString& kind) -> QString {
        const QString relative = tabOfKind(project, kind);
        if (relative.isEmpty()) {
            throw std::runtime_error(
                tr("The project has no enabled %1 tab.").arg(kind).toStdString());
        }
        return project.resolve(relative);
    };
    try {
        session->levelsPath_ = require(QStringLiteral("flicky.levels"));
        session->sourceRoot_ = project.resolve(QStringLiteral("flicky-source"));
        session->levels_ = FlickyLevelDocument::load(session->levelsPath_);
        session->savedLevels_ = session->levels_;
        loadTracked(session->graphics_, require(QStringLiteral("flicky.graphics")), session->sourceRoot_);
        loadTracked(session->semantics_, require(QStringLiteral("flicky.semantics")), session->sourceRoot_);
        loadTracked(session->sequences_, require(QStringLiteral("flicky.animations")), session->sourceRoot_);
        loadTracked(session->sound_, require(QStringLiteral("flicky.sound")), session->sourceRoot_);
        session->renderer_ = std::make_unique<FlickyLevelSceneRenderer>(
            session->sourceRoot_, session->graphics_.doc, session->semantics_.doc,
            session->sequences_.doc);
        QVector<FlickyLevelSceneRenderer::CastMember> cast;
        for (const QJsonValue& value : sdk::gameCatalog("title_cast").toArray()) {
            const QJsonObject member = value.toObject();
            cast.append({member.value(QStringLiteral("animation")).toString(),
                         member.value(QStringLiteral("label")).toString(),
                         member.value(QStringLiteral("text")).toString()});
        }
        session->renderer_->setTitleCast(cast);
        session->loadBuildRecord();
    } catch (const std::exception& e) {
        if (error) {
            *error = tr("Cannot open the Flicky project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile FlickySession::profile() const {
    sdk::Profile profile;
    profile.name = project_.string(QStringLiteral("project.name")).section(QLatin1Char(' '), 0, 0);
    if (profile.name.isEmpty()) {
        profile.name = tr("Flicky");
    }
    profile.badge = QStringLiteral("MD");
    profile.platform = tr("Mega Drive");
    profile.revision = QStringLiteral("UE");
    profile.cpu = QStringLiteral("68000");
    profile.chip = tr("YM2612 + PSG");
    profile.codec = QStringLiteral("flicky-content");
    return profile;
}

QVector<sdk::Kind> FlickySession::kinds() const {
    return {
        kind("levels", "Levels", sdk::EditorType::TileMap),
        kind("tiles", "Tiles", sdk::EditorType::Tile),
        kind("palettes", "Palettes", sdk::EditorType::Palette),
        kind("sprites", "Animations", sdk::EditorType::Composition),
        [] {
            sdk::Kind screens = kind("screens", "Screens", sdk::EditorType::TileMap);
            screens.icon = QStringLiteral("tileset");
            return screens;
        }(),
        kind("text", "Text", sdk::EditorType::Table),
        kind("sound", "Sound", sdk::EditorType::Sequencer),
        [] {
            sdk::Kind demo = kind("demo", "Demo", sdk::EditorType::Table);
            demo.icon = QStringLiteral("play");
            return demo;
        }(),
        kind("playtest", "Playtest", sdk::EditorType::Emulator),
    };
}

QString FlickySession::roundId(int round) {
    return QStringLiteral("round-") + two(round + 1);
}

int FlickySession::roundFromId(const QString& id) {
    bool ok = false;
    const int number = id.mid(6).toInt(&ok);
    return ok && id.startsWith(QLatin1String("round-")) && number >= 1 && number <= objects::kRounds
               ? number - 1
               : -1;
}

// ---------------------------------------------------------------- documents

QVector<sdk::DocumentGroup> FlickySession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("levels")) {
        for (int first = 0; first < objects::kRounds; first += 8) {
            sdk::DocumentGroup rounds = group(tr("Rounds %1–%2").arg(first + 1).arg(first + 8));
            for (int round = first; round < first + 8; ++round) {
                rounds.documents.append(entry(roundId(round), tr("Round %1").arg(two(round + 1)),
                                              objects::isBonusRound(round)
                                                  ? QStringLiteral("bonus")
                                                  : QStringLiteral("L") + two(levels_.layoutForRound(round)),
                                              isRoundDirty(round)));
            }
            groups.append(rounds);
        }
    } else if (kind == QLatin1String("tiles")) {
        const FlickyGraphicsDocument& g = graphics();
        sdk::DocumentGroup banks = group(tr("Tile banks"));
        for (const QString& id : g.tileAssetIds()) {
            banks.documents.append(entry(id, id,
                                         QStringLiteral("%1 · %2")
                                             .arg(g.tileCount(id))
                                             .arg(g.isFont(id) ? QStringLiteral("1bpp") : QStringLiteral("4bpp")),
                                         isBankDirty(id)));
        }
        groups.append(banks);
    } else if (kind == QLatin1String("screens")) {
        const FlickyGraphicsDocument& g = graphics();
        bool mapDirty = false;
        for (int i = 0; i < 48; ++i) {
            mapDirty = mapDirty || g.segaTilemapWord(i) != graphics_.saved.segaTilemapWord(i);
        }
        sdk::DocumentGroup screens = group(tr("Screens"));
        screens.documents.append(entry(QStringLiteral("sega_tilemap"), tr("SEGA logo"),
                                       QStringLiteral("12 × 4"), mapDirty));
        for (const QString& id : ScreenModel::documentIds()) {
            ScreenModel::Screen screen;
            ScreenModel::screenOf(id, &screen);
            bool textDirty = false;
            for (const QString& text : ScreenModel::textIds(this, screen)) {
                textDirty = textDirty || semantics().textValue(text) != semantics_.saved.textValue(text);
            }
            screens.documents.append(entry(id, ScreenModel::screenTitle(screen), QString(), textDirty));
        }
        groups.append(screens);
    } else if (kind == QLatin1String("palettes")) {
        const FlickySemanticsDocument& s = semantics();
        sdk::DocumentGroup level = group(tr("Level palettes"));
        sdk::DocumentGroup accent = group(tr("Accent palettes"));
        sdk::DocumentGroup other = group(tr("Other"));
        for (const QString& id : s.paletteIds()) {
            const QVector<int> colours = s.paletteColours(id);
            const sdk::DocumentEntry palette =
                entry(id, id, QString::number(colours.size()), colours != semantics_.saved.paletteColours(id));
            if (id.startsWith(QLatin1String("Level_AccentPalette"))) {
                accent.documents.append(palette);
            } else if (id.startsWith(QLatin1String("Level_Palette"))) {
                level.documents.append(palette);
            } else {
                other.documents.append(palette);
            }
        }
        bool segaDirty = false;
        for (int i = 0; i < 10; ++i) {
            segaDirty = segaDirty || !(graphics().segaPaletteEntry(i) == graphics_.saved.segaPaletteEntry(i));
        }
        other.documents.append(entry(QStringLiteral("sega_palette"), tr("SEGA logo"),
                                     tr("10 entries"), segaDirty));
        groups << level << accent << other;
    } else if (kind == QLatin1String("sprites")) {
        // Animations first; the frames they show (sprite mappings) below.
        sdk::DocumentGroup mappings = group(QStringLiteral("Frames"));
        for (const QString& id : sequences().mappingIds()) {
            const QJsonObject mapping = sequences().mapping(id);
            mappings.documents.append(entry(QStringLiteral("mapping:") + id, id,
                                            QString::number(mapping.value(QStringLiteral("pieces")).toArray().size()),
                                            mapping != sequences_.saved.mapping(id)));
        }
        sdk::DocumentGroup animations = group(tr("Animations"));
        for (const QString& id : sequences().animationIds()) {
            const QJsonObject animation = sequences().animation(id);
            animations.documents.append(entry(QStringLiteral("animation:") + id, id,
                                              tr("%1 fr").arg(animation.value(QStringLiteral("frames")).toArray().size()),
                                              animation != sequences_.saved.animation(id)));
        }
        sdk::DocumentGroup motion = group(tr("Bonus motion"));
        for (const QString& id : sequences().bonusMotionIds()) {
            const QJsonObject record = sequences().bonusMotion(id);
            motion.documents.append(entry(QStringLiteral("motion:") + id, id,
                                          record.value(QStringLiteral("kind")).toString(),
                                          record != sequences_.saved.bonusMotion(id)));
        }
        groups << animations << mappings;
        if (!motion.documents.isEmpty()) {
            groups << motion;
        }
    } else if (kind == QLatin1String("text")) {
        groups.append(group(tr("All game text"),
                            {entry(QStringLiteral("game-text"), tr("Game text"),
                                   tr("%n strings", nullptr, semantics().textIds().size()),
                                   !sameText(semantics(), semantics_.saved))}));
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup demos = group(tr("Attract demos"));
        if (!levels_.hasAttractDemos()) {
            // Opening it adds the demos from the project's own source.
            demos.documents.append(entry(QStringLiteral("demo-1"), tr("Attract demos"),
                                         tr("not added yet")));
        } else {
            for (int demo = 0; demo < 4; ++demo) {
                const QJsonArray steps = levels_.attractDemoSteps(demo);
                const int round = levels_.attractDemo(demo).value(QStringLiteral("round")).toInt();
                demos.documents.append(entry(QStringLiteral("demo-%1").arg(demo + 1),
                                             tr("Demo %1 · Round %2").arg(demo + 1).arg(two(round)),
                                             tr("%n steps", nullptr, steps.size()),
                                             savedLevels_.hasAttractDemos() &&
                                                 steps != savedLevels_.attractDemoSteps(demo)));
            }
        }
        groups.append(demos);
    } else if (kind == QLatin1String("sound")) {
        // Music first, then the sound effects (sfxheader blocks), then the
        // shared voices and data.
        sdk::DocumentGroup songs = group(tr("Music"));
        sdk::DocumentGroup effects = group(tr("Sound effects"));
        for (const QString& id : sound().headerIds()) {
            const FlickySoundHeader& header = sound().header(id);
            bool dirty = false;
            for (const FlickySoundTrack& track : header.tracks) {
                dirty = dirty || sound().blockText(track.sequence) != sound_.saved.blockText(track.sequence);
            }
            dirty = dirty || sound().blockText(id) != sound_.saved.blockText(id);
            const bool sfx = header.kind == QLatin1String("sfx");
            (sfx ? effects : songs)
                .documents.append(entry(QStringLiteral("header:") + id, id,
                                        tr("%1 ch").arg(header.tracks.size()), dirty));
        }
        sdk::DocumentGroup voices = group(tr("FM voices"));
        for (const QString& id : sound().voiceBankIds()) {
            voices.documents.append(entry(QStringLiteral("voice:") + id, id,
                                          tr("%n voices", nullptr, sound().voiceCount(id)),
                                          sound().blockText(id) != sound_.saved.blockText(id)));
        }
        sdk::DocumentGroup data = group(tr("Sound data"));
        data.documents.append(entry(QStringLiteral("envelopes"), tr("Priority & envelopes"),
                                    tr("%n bytes", nullptr, sound().priorityEnvelopeBytes().size()),
                                    sound().priorityEnvelopeBytes() != sound_.saved.priorityEnvelopeBytes()));
        groups << songs << effects << voices << data;
    } else if (kind == QLatin1String("playtest")) {
        groups.append(group(QString(), {entry(QStringLiteral("playtest"), tr("Build and run"),
                                              lastBuild().isEmpty() ? QString() : QStringLiteral("built"))}));
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> FlickySession::openEditor(const QString& kind,
                                                            const QString& document) {
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        if (round >= 0) {
            setPlayRound(round);
            if (objects::isBonusRound(round) && levels_.hasBonusPlacements()) {
                return std::make_unique<BonusRoundModel>(this, round);
            }
            return std::make_unique<LevelsModel>(this, round);
        }
    } else if (kind == QLatin1String("screens")) {
        if (document == QLatin1String("sega_tilemap")) {
            return std::make_unique<SegaTilemapModel>(this);
        }
        ScreenModel::Screen screen;
        if (ScreenModel::screenOf(document, &screen)) {
            // The title's sprite positions came with a newer kit.
            if (screen == ScreenModel::Screen::Title && !semantics().hasTitleLayout() && !titleUpgradeTried_) {
                titleUpgradeTried_ = true;
                upgradeProject(kind, document);
                return nullptr;
            }
            return std::make_unique<ScreenModel>(this, screen);
        }
    } else if (kind == QLatin1String("tiles")) {
        if (graphics().tileAssetIds().contains(document)) {
            return std::make_unique<TileBankModel>(this, document);
        }
    } else if (kind == QLatin1String("palettes")) {
        if (document == QLatin1String("sega_palette")) {
            return std::make_unique<SegaPaletteModel>(this);
        }
        if (semantics().paletteIds().contains(document)) {
            return std::make_unique<SemanticsPaletteModel>(this, document);
        }
    } else if (kind == QLatin1String("text")) {
        return std::make_unique<TextModel>(this);
    } else if (kind == QLatin1String("demo")) {
        if (!levels_.hasAttractDemos()) {
            upgradeProject(kind, document);
            return nullptr;
        }
        const int demo = document.startsWith(QLatin1String("demo-")) ? document.mid(5).toInt() - 1 : -1;
        if (demo >= 0 && demo < 4 && levels_.hasAttractDemos()) {
            return std::make_unique<FlickyDemoModel>(this, demo);
        }
    } else if (kind == QLatin1String("sprites")) {
        // Ids come from the document lists; anything else is refused here so
        // models never see an unknown record.
        const QString id = document.section(QLatin1Char(':'), 1);
        if (document.startsWith(QLatin1String("mapping:")) && sequences().mappingIds().contains(id)) {
            return std::make_unique<MappingModel>(this, id);
        }
        if (document.startsWith(QLatin1String("animation:")) && sequences().animationIds().contains(id)) {
            return std::make_unique<AnimationModel>(this, id);
        }
        if (document.startsWith(QLatin1String("motion:")) && sequences().bonusMotionIds().contains(id)) {
            return std::make_unique<BonusMotionModel>(this, id);
        }
    } else if (kind == QLatin1String("sound")) {
        const QString id = document.section(QLatin1Char(':'), 1);
        if (document.startsWith(QLatin1String("header:")) && sound().headerIds().contains(id)) {
            return std::make_unique<SoundHeaderModel>(this, id);
        }
        if (document.startsWith(QLatin1String("voice:")) && sound().voiceBankIds().contains(id)) {
            return std::make_unique<FmVoiceModel>(this, id);
        }
        if (document == QLatin1String("envelopes")) {
            return std::make_unique<EnvelopeModel>(this);
        }
    } else if (kind == QLatin1String("playtest")) {
        return std::make_unique<PlaytestModel>(this);
    }
    return nullptr;
}

// ---------------------------------------------------------------- levels

QVector<int> FlickySession::layoutsOfRound(int round) const {
    QVector<int> layouts{levels_.layoutForRound(round)};
    for (const QString& field : FlickyLevelDocument::objectFields()) {
        const int layout = levels_.objectLayoutForRound(round, field);
        if (!layouts.contains(layout)) {
            layouts.append(layout);
        }
    }
    return layouts;
}

bool FlickySession::isLayoutDirty(int layout) const {
    return levels_.grid(layout) != savedLevels_.grid(layout) || !sameObjects(levels_, savedLevels_, layout);
}

bool FlickySession::bonusPlacementsDirty() const {
    if (!levels_.hasBonusPlacements()) {
        return false;
    }
    for (const char* id : {"seesaw", "tiger", "chick_spawn"}) {
        if (levels_.bonusPlacement(QString::fromLatin1(id)) !=
            savedLevels_.bonusPlacement(QString::fromLatin1(id))) {
            return true;
        }
    }
    return false;
}

bool FlickySession::isRoundDirty(int round) const {
    if (objects::isBonusRound(round)) {
        return bonusPlacementsDirty();
    }
    for (int layout : layoutsOfRound(round)) {
        if (isLayoutDirty(layout)) {
            return true;
        }
    }
    return false;
}

bool FlickySession::roundDiffersFromBaseline(int round) const {
    if (objects::isBonusRound(round)) {
        if (!levels_.hasBonusPlacements()) {
            return false;
        }
        for (const char* id : {"seesaw", "tiger", "chick_spawn"}) {
            const QJsonObject p = levels_.bonusPlacement(QString::fromLatin1(id));
            if (p.value(QStringLiteral("x")) != p.value(QStringLiteral("baseline_x")) ||
                p.value(QStringLiteral("y")) != p.value(QStringLiteral("baseline_y"))) {
                return true;
            }
        }
        return false;
    }
    const int collision = levels_.layoutForRound(round);
    if (levels_.grid(collision) != levels_.baselineGrid(collision)) {
        return true;
    }
    for (const QString& field : FlickyLevelDocument::objectFields()) {
        const int layout = levels_.objectLayoutForRound(round, field);
        if (levels_.objectPositions(layout, field) != levels_.baselineObjectPositions(layout, field)) {
            return true;
        }
    }
    return false;
}

void FlickySession::replaceLevels(const FlickyLevelDocument& levels) {
    if (!levels.hasAttractDemos() && levels_.hasAttractDemos()) {
        // Snapshots from before the kit added the demos keep them.
        FlickyLevelDocument next = levels;
        next.adoptAttractDemos(levels_);
        levels_ = next;
    } else {
        levels_ = levels;
    }
    for (auto it = previews_.begin(); it != previews_.end();) {
        it = it.key().startsWith(QLatin1String("round:")) ? previews_.erase(it) : std::next(it);
    }
    emit levelsChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("levels"));
    emit documentsChanged(QStringLiteral("demo"));
    emit diagnosticsChanged();
}

void FlickySession::setLevelsLive(const FlickyLevelDocument& levels) {
    replaceLevels(levels);
}

void FlickySession::commitLevels(const FlickyLevelDocument& before, const FlickyLevelDocument& after,
                                 const QString& text) {
    // QUndoStack::push calls redo(), which applies `after`.
    undoStack()->push(new SnapshotCommand<FlickyLevelDocument>(
        [this](const FlickyLevelDocument& doc, quint64) { replaceLevels(doc); }, before, 0, after, 0,
        text));
}

bool FlickySession::editLevels(const QString& text,
                               const std::function<void(FlickyLevelDocument&)>& edit, QString* error) {
    FlickyLevelDocument next = levels_;
    try {
        edit(next);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    // The ROM keeps fixed space for all levels' data and special tiles: an
    // edit that grows past it is refused (one that frees bytes is fine
    // even while still over).
    QPair<int, int> limits;
    QPair<int, int> before;
    QPair<int, int> after;
    try {
        limits = next.capacityLimits();
        before = levels_.capacityUsage();
        after = next.capacityUsage();
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    if (after.first > limits.first && after.first > before.first) {
        if (error) {
            *error = tr("All levels share %1 bytes of level data; this would need %2. Remove something in any "
                        "level first.")
                         .arg(limits.first)
                         .arg(after.first);
        }
        return false;
    }
    if (after.second > limits.second && after.second > before.second) {
        if (error) {
            *error = tr("All levels share %1 bytes of special tiles; this would need %2. Remove one in any level "
                        "first.")
                         .arg(limits.second)
                         .arg(after.second);
        }
        return false;
    }
    commitLevels(levels_, next, text);
    return true;
}

// ---------------------------------------------------------------- graphics

void FlickySession::notifyGraphics() {
    previews_.clear();
    emit graphicsChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("tiles"));
    emit documentsChanged(QStringLiteral("levels"));
}

void FlickySession::setGraphics(const FlickyGraphicsDocument& doc, quint64 revision) {
    *graphics_.doc = doc;
    graphics_.revision = revision;
    notifyGraphics();
}

void FlickySession::touchGraphics() {
    graphics_.revision = graphics_.nextRevision++;
    notifyGraphics();
}

void FlickySession::commitGraphics(const FlickyGraphicsDocument& before, quint64 beforeRevision,
                                   const QString& text) {
    const quint64 afterRevision = graphics_.revision;
    undoStack()->push(new SnapshotCommand<FlickyGraphicsDocument>(
        [this](const FlickyGraphicsDocument& doc, quint64 revision) { setGraphics(doc, revision); },
        before, beforeRevision, *graphics_.doc, afterRevision, text));
}

bool FlickySession::editGraphics(const QString& text,
                                 const std::function<void(FlickyGraphicsDocument&)>& edit,
                                 QString* error) {
    FlickyGraphicsDocument next = *graphics_.doc;
    try {
        edit(next);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    const quint64 revision = graphics_.nextRevision++;
    undoStack()->push(new SnapshotCommand<FlickyGraphicsDocument>(
        [this](const FlickyGraphicsDocument& doc, quint64 rev) { setGraphics(doc, rev); },
        *graphics_.doc, graphics_.revision, next, revision, text));
    return true;
}

const QSet<int>& FlickySession::dirtyTiles(const QString& asset) const {
    if (dirtyTilesRevision_ != graphics_.revision) {
        dirtyTiles_.clear();
        dirtyTilesRevision_ = graphics_.revision;
    }
    auto it = dirtyTiles_.find(asset);
    if (it == dirtyTiles_.end()) {
        QSet<int> dirty;
        if (graphics_.isDirty()) {
            const int count = graphics().tileCount(asset);
            for (int tile = 0; tile < count; ++tile) {
                if (graphics().tile(asset, tile) != graphics_.saved.tile(asset, tile)) {
                    dirty.insert(tile);
                }
            }
        }
        it = dirtyTiles_.insert(asset, dirty);
    }
    return *it;
}

bool FlickySession::isTileDirty(const QString& asset, int tile) const {
    return dirtyTiles(asset).contains(tile);
}

bool FlickySession::isBankDirty(const QString& asset) const {
    return !dirtyTiles(asset).isEmpty();
}

std::array<int, 64> FlickySession::segaCram() const {
    std::array<int, 64> cram{};
    for (int entry = 0; entry < 10; ++entry) {
        const FlickyCompactPaletteEntry colour = graphics().segaPaletteEntry(entry);
        cram[static_cast<std::size_t>(colour.index)] = colour.colour;
    }
    return cram;
}

QImage FlickySession::segaScreen(int scale) const {
    const std::array<int, 64> cram = segaCram();
    QImage image(12 * 8, 4 * 8, QImage::Format_RGB32);
    image.fill(Qt::black);
    const QString bank = QStringLiteral("sega_tiles");
    const int tiles = graphics().tileCount(bank);
    for (int cell = 0; cell < 48; ++cell) {
        const megadrive::TileWord word = megadrive::decodeTileWord(graphics().segaTilemapWord(cell));
        const int left = cell % 12 * 8;
        const int top = cell / 12 * 8;
        if (word.pattern >= tiles) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    image.setPixel(left + x, top + y, qRgb(120, 28, 50));
                }
            }
            continue;
        }
        const megadrive::Tile pixels = graphics().tile(bank, word.pattern);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int sx = word.hflip ? 7 - x : x;
                const int sy = word.vflip ? 7 - y : y;
                const int index = word.palette * 16 + pixels[static_cast<std::size_t>(sy * 8 + sx)];
                image.setPixel(left + x, top + y,
                               megadrive::cramToArgb(cram[static_cast<std::size_t>(index)]));
            }
        }
    }
    return scale > 1 ? image.scaled(image.size() * scale) : image;
}

// ---------------------------------------------------------------- semantics

void FlickySession::notifySemantics() {
    previews_.clear();
    emit semanticsChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("palettes"));
    emit documentsChanged(QStringLiteral("text"));
    emit documentsChanged(QStringLiteral("levels"));
}

void FlickySession::touchSemantics() {
    semantics_.revision = semantics_.nextRevision++;
    notifySemantics();
}

void FlickySession::commitSemantics(const FlickySemanticsDocument& before, quint64 beforeRevision,
                                    const QString& text) {
    undoStack()->push(new SnapshotCommand<FlickySemanticsDocument>(
        [this](const FlickySemanticsDocument& doc, quint64 revision) { setSemantics(doc, revision); }, before,
        beforeRevision, *semantics_.doc, semantics_.revision, text));
}

void FlickySession::setSemantics(const FlickySemanticsDocument& doc, quint64 revision) {
    if (!doc.hasTitleLayout() && semantics_.doc->hasTitleLayout()) {
        // Snapshots from before the kit added the title layout keep it.
        FlickySemanticsDocument next = doc;
        next.adoptTitleLayout(*semantics_.doc);
        *semantics_.doc = next;
    } else {
        *semantics_.doc = doc;
    }
    semantics_.revision = revision;
    notifySemantics();
}

bool FlickySession::editSemantics(const QString& text,
                                  const std::function<void(FlickySemanticsDocument&)>& edit,
                                  QString* error) {
    FlickySemanticsDocument next = *semantics_.doc;
    try {
        edit(next);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    const quint64 revision = semantics_.nextRevision++;
    undoStack()->push(new SnapshotCommand<FlickySemanticsDocument>(
        [this](const FlickySemanticsDocument& doc, quint64 rev) { setSemantics(doc, rev); },
        *semantics_.doc, semantics_.revision, next, revision, text));
    return true;
}

// ---------------------------------------------------------------- previews

QImage FlickySession::placementPreview(const QString& field, int round) const {
    const QString key = QStringLiteral("place:%1:%2").arg(field).arg(round);
    auto it = previews_.constFind(key);
    if (it != previews_.constEnd()) {
        return *it;
    }
    QImage image;
    try {
        image = renderer_->renderPlacementPreview(field, round);
    } catch (const std::exception&) {
        image = QImage();
    }
    previews_.insert(key, image);
    return image;
}

QImage FlickySession::roundPreview(int round) const {
    const QString key = QStringLiteral("round:%1").arg(round);
    auto it = previews_.constFind(key);
    if (it != previews_.constEnd()) {
        return *it;
    }
    QImage image;
    try {
        image = renderer_->renderLayer(levels_, round, FlickyLevelSceneRenderer::Layer::Objects);
    } catch (const std::exception&) {
        image = QImage();
    }
    previews_.insert(key, image);
    return image;
}

QString FlickySession::previewPalette(const QString& asset) const {
    return previewPalettes_.value(asset);
}

void FlickySession::setPreviewPalette(const QString& asset, const QString& palette) {
    previewPalettes_.insert(asset, palette);
}

// ---------------------------------------------------------------- saving

bool FlickySession::save(QString* error) {
    const auto failed = [error](const QString& path, const std::exception& e) {
        if (error) {
            *error = tr("Cannot save %1: %2").arg(QFileInfo(path).fileName(),
                                                              QString::fromUtf8(e.what()));
        }
        return false;
    };
    bool levelsDirty = bonusPlacementsDirty() || attractDemosDirty();
    for (int layout = 0; layout < objects::kLayouts && !levelsDirty; ++layout) {
        levelsDirty = isLayoutDirty(layout);
    }
    // Each document on its own: one that cannot be written (a level over
    // its capacity) leaves the others saved, and only itself unsaved.
    bool ok = true;
    const auto write = [&](bool dirty, const QString& path, const std::function<void()>& save) {
        if (!dirty) return true;
        try {
            save();
            return true;
        } catch (const std::exception& e) {
            if (ok) failed(path, e);
            ok = false;
            return false;
        }
    };
    if (write(levelsDirty, levelsPath_, [&] { levels_.save(levelsPath_); })) {
        savedLevels_ = levels_;
    }
    if (write(graphics_.isDirty(), graphics_.path, [&] { graphics_.doc->save(graphics_.path); })) {
        graphics_.saved = *graphics_.doc;
        graphics_.savedRevision = graphics_.revision;
    }
    if (write(semantics_.isDirty(), semantics_.path, [&] { semantics_.doc->save(semantics_.path); })) {
        semantics_.saved = *semantics_.doc;
        semantics_.savedRevision = semantics_.revision;
    }
    if (write(sequences_.isDirty(), sequences_.path, [&] { sequences_.doc->save(sequences_.path); })) {
        sequences_.saved = *sequences_.doc;
        sequences_.savedRevision = sequences_.revision;
    }
    if (write(sound_.isDirty(), sound_.path, [&] { sound_.doc->save(sound_.path); })) {
        sound_.saved = *sound_.doc;
        sound_.savedRevision = sound_.revision;
    }
    dirtyTilesRevision_ = ~quint64(0);
    if (ok) undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return ok;
}

bool FlickySession::canRevert(const QString& kind, const QString& document) const {
    // The SEGA logo's map and palette live in graphics.json with the tiles.
    if ((kind == QLatin1String("screens") && document == QLatin1String("sega_tilemap")) ||
        (kind == QLatin1String("palettes") && document == QLatin1String("sega_palette"))) {
        return canRevert(QStringLiteral("tiles"), document);
    }
    ScreenModel::Screen screen;
    if (kind == QLatin1String("screens") && ScreenModel::screenOf(document, &screen)) {
        for (const QString& id : ScreenModel::textIds(this, screen)) {
            if (semantics().textValue(id) != semantics().baselineTextValue(id)) return true;
        }
        if (screen == ScreenModel::Screen::Title) {
            for (const QString& group : {QStringLiteral("statics"), QStringLiteral("cast")}) {
                if (semantics().titlePositions(group) != semantics().baselineTitlePositions(group)) return true;
            }
        }
        return false;
    }
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        return round >= 0 && roundDiffersFromBaseline(round);
    }
    if (kind == QLatin1String("tiles")) {
        const FlickyGraphicsDocument& g = graphics();
        if (document == QLatin1String("sega_palette")) {
            for (int i = 0; i < 10; ++i) {
                if (!(g.segaPaletteEntry(i) == g.baselineSegaPaletteEntry(i))) return true;
            }
            return false;
        }
        if (document == QLatin1String("sega_tilemap")) {
            for (int i = 0; i < 48; ++i) {
                if (g.segaTilemapWord(i) != g.baselineSegaTilemapWord(i)) return true;
            }
            return false;
        }
        if (g.tileAssetIds().contains(document)) {
            const int count = g.tileCount(document);
            for (int tile = 0; tile < count; ++tile) {
                if (g.tile(document, tile) != g.baselineTile(document, tile)) return true;
            }
        }
        return false;
    }
    if (kind == QLatin1String("palettes")) {
        return semantics().paletteIds().contains(document) &&
               semantics().paletteColours(document) != semantics().baselinePaletteColours(document);
    }
    if (kind == QLatin1String("text")) {
        for (const QString& id : semantics().textIds()) {
            if (semantics().textValue(id) != semantics().baselineTextValue(id)) return true;
        }
    }
    if (kind == QLatin1String("demo") && document.startsWith(QLatin1String("demo-")) && levels_.hasAttractDemos()) {
        const int demo = document.mid(5).toInt() - 1;
        return demo >= 0 && demo < 4 && levels_.attractDemoSteps(demo) != levels_.baselineAttractDemoSteps(demo);
    }
    if (kind == QLatin1String("sprites")) {
        const QString id = document.section(QLatin1Char(':'), 1);
        if (document.startsWith(QLatin1String("mapping:"))) return sequences().mapping(id) != sequences().baselineMapping(id);
        if (document.startsWith(QLatin1String("animation:"))) return sequences().animation(id) != sequences().baselineAnimation(id);
        if (document.startsWith(QLatin1String("motion:"))) return sequences().bonusMotion(id) != sequences().baselineBonusMotion(id);
    }
    if (kind == QLatin1String("sound")) {
        if (document.startsWith(QLatin1String("header:"))) {
            const QString id = document.mid(7);
            for (const FlickySoundTrack& track : sound().header(id).tracks) {
                if (sound().blockText(track.sequence) != sound().baselineBlockText(track.sequence)) return true;
            }
            return sound().blockText(id) != sound().baselineBlockText(id);
        }
        if (document.startsWith(QLatin1String("voice:"))) {
            return sound().blockText(document.mid(6)) != sound().baselineBlockText(document.mid(6));
        }
        if (document == QLatin1String("envelopes")) {
            return sound().blockText(QStringLiteral("zPriorityAndEnvelopeData")) !=
                   sound().baselineBlockText(QStringLiteral("zPriorityAndEnvelopeData"));
        }
    }
    return false;
}

bool FlickySession::revert(const QString& kind, const QString& document, QString* error) {
    if ((kind == QLatin1String("screens") && document == QLatin1String("sega_tilemap")) ||
        (kind == QLatin1String("palettes") && document == QLatin1String("sega_palette"))) {
        return revert(QStringLiteral("tiles"), document, error);
    }
    ScreenModel::Screen screen;
    if (kind == QLatin1String("screens") && ScreenModel::screenOf(document, &screen)) {
        const QStringList ids = ScreenModel::textIds(this, screen);
        return editSemantics(tr("Revert %1").arg(ScreenModel::screenTitle(screen)),
                             [ids, screen](FlickySemanticsDocument& s) {
                                 for (const QString& id : ids) s.restoreText(id);
                                 if (screen == ScreenModel::Screen::Title) s.restoreTitleLayout();
                             },
                             error);
    }
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        if (round < 0) {
            return Session::revert(kind, document, error);
        }
        if (objects::isBonusRound(round)) {
            return editLevels(tr("Revert bonus placements"),
                              [](FlickyLevelDocument& levels) {
                                  for (const char* id : {"seesaw", "tiger", "chick_spawn"}) {
                                      levels.restoreBonusPlacement(QString::fromLatin1(id));
                                  }
                              },
                              error);
        }
        return editLevels(
            tr("Revert Round %1").arg(two(round + 1)),
            [round](FlickyLevelDocument& levels) {
                levels.restoreLayout(levels.layoutForRound(round));
                for (const QString& field : FlickyLevelDocument::objectFields()) {
                    levels.restoreObjectGroup(levels.objectLayoutForRound(round, field), field);
                }
            },
            error);
    }
    if (kind == QLatin1String("tiles")) {
        return editGraphics(tr("Revert %1").arg(document),
                            [document](FlickyGraphicsDocument& g) {
                                if (document == QLatin1String("sega_palette")) {
                                    for (int i = 0; i < 10; ++i) {
                                        const auto e = g.baselineSegaPaletteEntry(i);
                                        g.setSegaPaletteEntry(i, e.index, e.colour);
                                    }
                                } else if (document == QLatin1String("sega_tilemap")) {
                                    for (int i = 0; i < 48; ++i) {
                                        g.setSegaTilemapWord(i, g.baselineSegaTilemapWord(i));
                                    }
                                } else {
                                    const int count = g.tileCount(document);
                                    for (int tile = 0; tile < count; ++tile) {
                                        g.restoreTile(document, tile);
                                    }
                                }
                            },
                            error);
    }
    if (kind == QLatin1String("palettes")) {
        return editSemantics(tr("Revert %1").arg(document),
                             [document](FlickySemanticsDocument& s) { s.restorePalette(document); },
                             error);
    }
    if (kind == QLatin1String("sprites")) {
        const QString id = document.section(QLatin1Char(':'), 1);
        return editSequences(tr("Revert %1").arg(id),
                             [document, id](FlickySequencesDocument& q) {
                                 if (document.startsWith(QLatin1String("mapping:"))) q.restoreMapping(id);
                                 else if (document.startsWith(QLatin1String("animation:"))) q.restoreAnimation(id);
                                 else q.restoreBonusMotion(id);
                             },
                             error);
    }
    if (kind == QLatin1String("sound")) {
        return editSound(tr("Revert %1").arg(document),
                         [document](FlickySoundDocument& d) {
                             if (document.startsWith(QLatin1String("header:"))) {
                                 const QString id = document.mid(7);
                                 for (const FlickySoundTrack& track : d.header(id).tracks) {
                                     d.restoreBlock(track.sequence);
                                 }
                                 d.restoreBlock(id);
                             } else if (document.startsWith(QLatin1String("voice:"))) {
                                 d.restoreBlock(document.mid(6));
                             } else {
                                 d.restoreBlock(QStringLiteral("zPriorityAndEnvelopeData"));
                             }
                         },
                         error);
    }
    if (kind == QLatin1String("demo") && canRevert(kind, document)) {
        const int demo = document.mid(5).toInt() - 1;
        return editDemo(demo, tr("Revert demo %1").arg(demo + 1), levels_.baselineAttractDemoSteps(demo),
                        error);
    }
    if (kind == QLatin1String("text")) {
        return editSemantics(tr("Revert game text"),
                             [](FlickySemanticsDocument& s) {
                                 for (const QString& id : s.textIds()) {
                                     s.restoreText(id);
                                 }
                             },
                             error);
    }
    return Session::revert(kind, document, error);
}

// ---------------------------------------------------------------- diagnostics

QVector<sdk::Diagnostic> FlickySession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    const QPair<int, int> used = levels_.capacityUsage();
    const QPair<int, int> limit = levels_.capacityLimits();
    const auto check = [&](const QString& what, int value, int capacity) {
        sdk::Diagnostic d;
        d.source = QStringLiteral("levels");
        d.location = QStringLiteral("levels.json");
        d.kind = QStringLiteral("levels");
        if (value > capacity) {
            d.severity = sdk::Severity::Error;
            d.message = tr("%1 uses %2 of %3 bytes; the ROM has no room for %4 more.")
                            .arg(what).arg(value).arg(capacity).arg(value - capacity);
        } else {
            d.severity = value * 100 >= capacity * 98 ? sdk::Severity::Warning : sdk::Severity::Info;
            d.message = tr("%1 uses %2 of %3 bytes.").arg(what).arg(value).arg(capacity);
        }
        result.append(d);
    };
    check(tr("Level data"), used.first, limit.first);
    check(tr("Special tile data"), used.second, limit.second);
    return result;
}

QString FlickySession::selectedStart() const {
    if (demoStart()) {
        return tr("Demo %1").arg(demo_ + 1);
    }
    return tr("Round %1").arg(two(playRound_ + 1));
}

void FlickySession::setPlayRound(int round) {
    if (round >= 0 && round < objects::kRounds && round != playRound_) {
        playRound_ = round;
        emit playRoundChanged();
        emit selectedStartChanged();
    }
    setPaletteRound(round);
}

void FlickySession::setPaletteRound(int round) {
    if (round >= 0 && round < objects::kRounds && round != paletteRound_) {
        paletteRound_ = round;
        emit paletteRoundChanged();
    }
}

// ---------------------------------------------------------------- tracked documents

void FlickySession::notifySequences() {
    previews_.clear();
    emit sequencesChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("sprites"));
    emit documentsChanged(QStringLiteral("levels"));
}

void FlickySession::touchSequences() {
    sequences_.revision = sequences_.nextRevision++;
    notifySequences();
}

void FlickySession::commitSequences(const FlickySequencesDocument& before, quint64 beforeRevision,
                                    const QString& text) {
    undoStack()->push(new SnapshotCommand<FlickySequencesDocument>(
        [this](const FlickySequencesDocument& doc, quint64 revision) {
            *sequences_.doc = doc;
            sequences_.revision = revision;
            notifySequences();
        },
        before, beforeRevision, *sequences_.doc, sequences_.revision, text));
}

bool FlickySession::editSequences(const QString& text,
                                  const std::function<void(FlickySequencesDocument&)>& edit,
                                  QString* error) {
    FlickySequencesDocument next = *sequences_.doc;
    try {
        edit(next);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    const quint64 revision = sequences_.nextRevision++;
    undoStack()->push(new SnapshotCommand<FlickySequencesDocument>(
        [this](const FlickySequencesDocument& doc, quint64 rev) {
            *sequences_.doc = doc;
            sequences_.revision = rev;
            notifySequences();
        },
        *sequences_.doc, sequences_.revision, next, revision, text));
    return true;
}

void FlickySession::notifySound() {
    emit soundChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("sound"));
}

void FlickySession::touchSound() {
    sound_.revision = sound_.nextRevision++;
    notifySound();
}

void FlickySession::commitSound(const FlickySoundDocument& before, quint64 beforeRevision, const QString& text) {
    undoStack()->push(new SnapshotCommand<FlickySoundDocument>(
        [this](const FlickySoundDocument& doc, quint64 revision) {
            *sound_.doc = doc;
            sound_.revision = revision;
            notifySound();
        },
        before, beforeRevision, *sound_.doc, sound_.revision, text));
}

bool FlickySession::editSound(const QString& text, const std::function<void(FlickySoundDocument&)>& edit,
                              QString* error) {
    FlickySoundDocument next = *sound_.doc;
    try {
        edit(next);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    const quint64 revision = sound_.nextRevision++;
    undoStack()->push(new SnapshotCommand<FlickySoundDocument>(
        [this](const FlickySoundDocument& doc, quint64 rev) {
            *sound_.doc = doc;
            sound_.revision = rev;
            notifySound();
        },
        *sound_.doc, sound_.revision, next, revision, text));
    return true;
}

// ---------------------------------------------------------------- build and play

namespace {

}  // namespace

QByteArray FlickySession::buildInputs(const QString& writer) const {
    const QDir root(project_.directory());
    QStringList files;
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString relative = root.relativeFilePath(it.next());
        if (!relative.split(QLatin1Char('/')).contains(QLatin1String("build"))) {
            files << relative;
        }
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const auto add = [&hash](const QString& name, const QFileInfo& info) {
        hash.addData(name.toUtf8());
        hash.addData(QByteArray::number(info.size()));
        hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    };
    for (const QString& file : files) {
        add(file, QFileInfo(root.filePath(file)));
    }
    add(writer, QFileInfo(writer));
    return hash.result();
}

QString FlickySession::buildRecordPath() const {
    return QDir(project_.directory()).filePath(QStringLiteral("build/studio-build.json"));
}

void FlickySession::loadBuildRecord() {
    QFile file(buildRecordPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject record = QJsonDocument::fromJson(file.readAll()).object();
    builtInputs_ = QByteArray::fromHex(record.value(QStringLiteral("inputs")).toString().toLatin1());
    builtRom_ = record.value(QStringLiteral("rom")).toString();
    const QJsonObject plans = record.value(QStringLiteral("plans")).toObject();
    for (auto it = plans.constBegin(); it != plans.constEnd(); ++it) {
        const QJsonObject plan = it.value().toObject();
        sdk::ProcessSpec spec;
        spec.program = plan.value(QStringLiteral("program")).toString();
        for (const QJsonValue& argument : plan.value(QStringLiteral("arguments")).toArray()) {
            spec.arguments << argument.toString();
        }
        spec.workingDirectory = plan.value(QStringLiteral("working_directory")).toString();
        const QJsonObject environment = plan.value(QStringLiteral("environment")).toObject();
        for (auto variable = environment.constBegin(); variable != environment.constEnd(); ++variable) {
            spec.environment.insert(variable.key(), variable.value().toString());
        }
        if (!spec.program.isEmpty()) {
            plans_.insert(it.key(), spec);
        }
    }
}

void FlickySession::saveBuildRecord() const {
    QJsonObject plans;
    for (auto it = plans_.constBegin(); it != plans_.constEnd(); ++it) {
        QJsonObject environment;
        for (auto variable = it->environment.constBegin(); variable != it->environment.constEnd(); ++variable) {
            environment.insert(variable.key(), variable.value());
        }
        plans.insert(it.key(), QJsonObject{{QStringLiteral("program"), it->program},
                                           {QStringLiteral("arguments"), QJsonArray::fromStringList(it->arguments)},
                                           {QStringLiteral("working_directory"), it->workingDirectory},
                                           {QStringLiteral("environment"), environment}});
    }
    QFile file(buildRecordPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("inputs"), QString::fromLatin1(builtInputs_.toHex())},
                                             {QStringLiteral("rom"), builtRom_},
                                             {QStringLiteral("plans"), plans}})
                       .toJson());
    }
}

bool FlickySession::prepareBuild(BuildStep& step, QString* error) {
    const QString writer = host()->toolPath(QStringLiteral("retrolarium-flicky-level-writer"));
    if (writer.isEmpty()) {
        if (error) *error = tr("retrolarium-flicky-level-writer is missing from the studio's tools folder. Reinstall Retrolarium Studio.");
        return false;
    }
    step.inputs = buildInputs(writer);
    step.upToDate = !step.force && step.inputs == builtInputs_ && QFileInfo::exists(builtRom_);
    step.arguments = QStringList{QStringLiteral("--flicky-level-writer"), writer};
    step.environment.insert(QStringLiteral("RETROLARIUM_FLICKY_LEVEL_WRITER"), writer);
    return true;
}

void FlickySession::built(const BuildStep& step, const QJsonObject& artifact) {
    builtInputs_ = step.inputs;
    builtRom_ = QDir(project_.directory()).absoluteFilePath(artifact.value(QStringLiteral("path")).toString());
    plans_.clear();
    saveBuildRecord();
}

void FlickySession::build() {
    // An explicit Build always runs the kit.
    runBuild([](bool) {}, true);
}

void FlickySession::play() {
    launch(-1);
}

void FlickySession::playSelected() {
    if (isRecording()) {
        stopRecording();
    } else if (demoStart()) {
        launchDemo(demo_, false);
    } else {
        launch(playRound_);
    }
}

QString FlickySession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

// ---------------------------------------------------------------- attract demos

bool FlickySession::attractDemosDirty() const {
    if (!levels_.hasAttractDemos() || !savedLevels_.hasAttractDemos()) {
        return levels_.hasAttractDemos() != savedLevels_.hasAttractDemos();
    }
    for (int demo = 0; demo < 4; ++demo) {
        if (levels_.attractDemoSteps(demo) != savedLevels_.attractDemoSteps(demo)) {
            return true;
        }
    }
    return false;
}

bool FlickySession::editDemo(int demo, const QString& text, const QJsonArray& steps, QString* error) {
    if (!levels_.hasAttractDemos() || levels_.attractDemoSteps(demo) == steps) {
        return true;
    }
    return editLevels(text, [demo, steps](FlickyLevelDocument& levels) { levels.setAttractDemoSteps(demo, steps); },
                      error);
}

int FlickySession::demoStepCapacity(int demo) const {
    const int others = levels_.attractDemoBytes() - 2 * levels_.attractDemoSteps(demo).size();
    return (levels_.attractDemoCapacity() - others) / 2;
}

void FlickySession::attachDemoEditor(int demo, bool attach) {
    const QString before = selectedStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (attach) {
        demo_ = demo;
    }
    if (selectedStart() != before) {
        emit selectedStartChanged();
    }
}

void FlickySession::playDemo(int demo, bool record) {
    if (!isRecording()) {
        launchDemo(demo, record);
    }
}

void FlickySession::upgradeProject(const QString& kind, const QString& document) {
    UpgradeStep step;
    step.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("flicky-upgrade"), project_.path()};
    step.label = tr("Add what this kit edits to the project");
    step.source = kind;
    step.failure = tr("Upgrading the project failed: %1");
    step.adopt = [this, kind] {
        // The kit adds sections to the saved documents; unsaved edits stay
        // as they are.
        const FlickyLevelDocument upgraded = FlickyLevelDocument::load(levelsPath_);
        if (!levels_.hasAttractDemos() && upgraded.hasAttractDemos()) {
            savedLevels_.adoptAttractDemos(upgraded);
            levels_.adoptAttractDemos(upgraded);
        }
        const FlickySemanticsDocument semantics = FlickySemanticsDocument::load(semantics_.path);
        if (!semantics_.doc->hasTitleLayout() && semantics.hasTitleLayout()) {
            semantics_.saved.adoptTitleLayout(semantics);
            semantics_.doc->adoptTitleLayout(semantics);
        }
        if (kind == QLatin1String("demo") && !levels_.hasAttractDemos()) {
            throw std::runtime_error(tr("The kit added no attract demos.").toStdString());
        }
    };
    step.done = tr("Added the documents and data this kit edits to the project.");
    step.openKind = kind;
    step.openDocument = kind == QLatin1String("demo") ? QStringLiteral("demo-1") : document;
    runUpgrade(step);
}

void FlickySession::launchDemo(int demo, bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("flicky-demo-plan"), project_.path(),
                      QStringLiteral("--demo"), QString::number(demo + 1)};
    plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
    plan.title = record ? tr("Flicky · recording demo %1").arg(demo + 1) : tr("Flicky · demo %1").arg(demo + 1);
    plan.demo = true;
    plan.force = false;
    // The Playtest view covers the way to the demo.
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("FLICKY_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage = tr("Recording: play the round with the controller; it ends when the bird is "
                                   "caught, on Start or on Stop recording.");
        plan.recorded = [this, demo](const QString& path) { applyRecording(demo, path); };
    }
    launchPlanned(plan);
}

void FlickySession::applyRecording(int demo, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const int capacity = demoStepCapacity(demo);
    const FlickyDemoModel::Recording recording =
        FlickyDemoModel::stepsFromRecording(QString::fromUtf8(file.readAll()), capacity);
    if (!recording.pressed) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               tr("No buttons were pressed while recording; the demo stays as it was."));
        return;
    }
    QString error;
    if (!editDemo(demo, tr("Record demo %1").arg(demo + 1), recording.steps, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    QString message = tr("Recorded %1 s of input into demo %2 (%3 steps).")
                          .arg(recording.keptFrames / 60.0, 0, 'f', 1)
                          .arg(demo + 1)
                          .arg(recording.steps.size());
    if (recording.droppedFrames > 0) {
        message += tr(" The demos share %1 bytes; the last %2 s did not fit.")
                       .arg(levels_.attractDemoCapacity())
                       .arg(recording.droppedFrames / 60.0, 0, 'f', 1);
    }
    report(recording.droppedFrames > 0 ? sdk::Severity::Warning : sdk::Severity::Info, QStringLiteral("demo"), message);
}

void FlickySession::launch(int round) {
    const bool fromRound = round >= 0;
    EmulatorPlan plan;
    plan.force = false;
    if (fromRound) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("flicky-round-playtest-plan"), project_.path(),
                          QStringLiteral("--round"), QString::number(round + 1)};
        plan.label = tr("Plan playtest of Round %1").arg(two(round + 1));
        plan.title = tr("Flicky · Round %1").arg(two(round + 1));
        // The kit's playtest script writes one status line here once the
        // round is on screen (or could not be reached).
        plan.readyFile = QStringLiteral("build/playtest-status.txt");
        plan.readyVariable = QStringLiteral("FLICKY_PLAYTEST_RESULT");
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("Flicky · power-on");
    }
    // The kit's plan for an unchanged ROM is kept (build/studio-build.json):
    // asking again costs a second or two.
    const QString key = fromRound ? QStringLiteral("round:%1").arg(round) : QStringLiteral("power-on");
    const auto keyed = [this, key] {
        return key + QLatin1Char('|') + (host() ? host()->emulatorPath(QStringLiteral("mame")) : QString());
    };
    plan.cached = [this, keyed](sdk::ProcessSpec* spec) {
        const auto planned = plans_.constFind(keyed());
        if (planned == plans_.constEnd()) return false;
        // The record is a file in the project: a plan whose program is not
        // the MAME chosen in Settings is asked for again, never run.
        const QString mame = host() ? host()->emulatorPath(QStringLiteral("mame")) : QString();
        if (mame.isEmpty() || QFileInfo(planned->program).canonicalFilePath() != QFileInfo(mame).canonicalFilePath()) {
            return false;
        }
        *spec = *planned;
        return true;
    };
    plan.remember = [this, keyed](const sdk::ProcessSpec& spec) {
        plans_.insert(keyed(), spec);
        saveBuildRecord();
    };
    launchPlanned(plan);
}

}  // namespace rl::flicky
