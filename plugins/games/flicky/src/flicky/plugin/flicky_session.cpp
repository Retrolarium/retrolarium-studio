#include "flicky/plugin/flicky_session.hpp"

#include "flicky/plugin/graphics_models.hpp"
#include "flicky/plugin/levels_model.hpp"
#include "flicky/plugin/objects.hpp"
#include "flicky/plugin/semantics_models.hpp"
#include "megadrive/vdp.hpp"

#include "flicky/plugin/bonus_model.hpp"
#include "flicky/plugin/playtest_model.hpp"
#include "flicky/plugin/sound_models.hpp"
#include "flicky/plugin/sprite_models.hpp"
#include "sdk/snapshot_command.hpp"
#include "sdk/plugin.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoCommand>
#include <QUndoStack>

namespace rl::flicky {

namespace {

// One edit: swaps between two whole-document states. Documents keep their
// JSON implicitly shared, so a snapshot costs little until it is changed, and
// every edit, including multi-field ones, is exactly reversible.
using sdk::SnapshotCommand;

QString tabDocument(const sdk::ProjectFile& project, const QString& kind) {
    for (const QVariant& value : project.list(QStringLiteral("workspace.tabs"))) {
        const QVariantMap tab = value.toMap();
        if (tab.value(QStringLiteral("kind")).toString() == kind &&
            tab.value(QStringLiteral("enabled"), true).toBool()) {
            return tab.value(QStringLiteral("document")).toString();
        }
    }
    return {};
}

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
void loadTracked(Tracked<T>& tracked, const QString& path) {
    tracked.path = path;
    tracked.doc = std::make_shared<T>(T::load(path));
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
        const QString relative = tabDocument(project, kind);
        if (relative.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("The project has no enabled %1 tab.").arg(kind).toStdString());
        }
        return project.resolve(relative);
    };
    try {
        session->levelsPath_ = require(QStringLiteral("flicky.levels"));
        session->sourceRoot_ = project.resolve(QStringLiteral("flicky-source"));
        session->levels_ = FlickyLevelDocument::load(session->levelsPath_);
        session->savedLevels_ = session->levels_;
        loadTracked(session->graphics_, require(QStringLiteral("flicky.graphics")));
        loadTracked(session->semantics_, require(QStringLiteral("flicky.semantics")));
        loadTracked(session->sequences_, require(QStringLiteral("flicky.animations")));
        loadTracked(session->sound_, require(QStringLiteral("flicky.sound")));
        session->renderer_ = std::make_unique<FlickyLevelSceneRenderer>(
            session->sourceRoot_, session->graphics_.doc, session->semantics_.doc,
            session->sequences_.doc);
        session->loadBuildRecord();
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("Cannot open the Flicky project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile FlickySession::profile() const {
    sdk::Profile profile;
    profile.name = project_.string(QStringLiteral("project.name")).section(QLatin1Char(' '), 0, 0);
    if (profile.name.isEmpty()) {
        profile.name = QStringLiteral("Flicky");
    }
    profile.badge = QStringLiteral("MD");
    profile.platform = QStringLiteral("Mega Drive");
    profile.revision = QStringLiteral("UE");
    profile.cpu = QStringLiteral("68000");
    profile.chip = QStringLiteral("YM2612 + PSG");
    profile.codec = QStringLiteral("flicky-content");
    return profile;
}

QVector<sdk::Kind> FlickySession::kinds() const {
    return {
        kind("levels", "Levels", sdk::EditorType::TileMap),
        kind("tiles", "Tiles", sdk::EditorType::Tile),
        kind("palettes", "Palettes", sdk::EditorType::Palette),
        kind("sprites", "Sprites", sdk::EditorType::Composition),
        kind("text", "Text", sdk::EditorType::Table),
        kind("sound", "Sound", sdk::EditorType::Sequencer),
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
            sdk::DocumentGroup rounds = group(QStringLiteral("Rounds %1–%2").arg(first + 1).arg(first + 8));
            for (int round = first; round < first + 8; ++round) {
                rounds.documents.append(entry(roundId(round), QStringLiteral("Round ") + two(round + 1),
                                              objects::isBonusRound(round)
                                                  ? QStringLiteral("bonus")
                                                  : QStringLiteral("L") + two(levels_.layoutForRound(round)),
                                              isRoundDirty(round)));
            }
            groups.append(rounds);
        }
    } else if (kind == QLatin1String("tiles")) {
        const FlickyGraphicsDocument& g = graphics();
        sdk::DocumentGroup banks = group(QStringLiteral("Tile banks"));
        for (const QString& id : g.tileAssetIds()) {
            banks.documents.append(entry(id, id,
                                         QStringLiteral("%1 · %2")
                                             .arg(g.tileCount(id))
                                             .arg(g.isFont(id) ? QStringLiteral("1bpp") : QStringLiteral("4bpp")),
                                         isBankDirty(id)));
        }
        groups.append(banks);
        bool paletteDirty = false;
        for (int i = 0; i < 10; ++i) {
            paletteDirty = paletteDirty || !(g.segaPaletteEntry(i) == graphics_.saved.segaPaletteEntry(i));
        }
        bool mapDirty = false;
        for (int i = 0; i < 48; ++i) {
            mapDirty = mapDirty || g.segaTilemapWord(i) != graphics_.saved.segaTilemapWord(i);
        }
        groups.append(group(QStringLiteral("SEGA screen"),
                            {entry(QStringLiteral("sega_palette"), QStringLiteral("sega_palette"),
                                   QStringLiteral("10 entries"), paletteDirty),
                             entry(QStringLiteral("sega_tilemap"), QStringLiteral("sega_tilemap"),
                                   QStringLiteral("12 × 4"), mapDirty)}));
    } else if (kind == QLatin1String("palettes")) {
        const FlickySemanticsDocument& s = semantics();
        sdk::DocumentGroup level = group(QStringLiteral("Level palettes"));
        sdk::DocumentGroup accent = group(QStringLiteral("Accent palettes"));
        sdk::DocumentGroup other = group(QStringLiteral("Other"));
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
        groups << level << accent << other;
    } else if (kind == QLatin1String("sprites")) {
        sdk::DocumentGroup mappings = group(QStringLiteral("Mappings"));
        for (const QString& id : sequences().mappingIds()) {
            const QJsonObject mapping = sequences().mapping(id);
            mappings.documents.append(entry(QStringLiteral("mapping:") + id, id,
                                            QString::number(mapping.value(QStringLiteral("pieces")).toArray().size()),
                                            mapping != sequences_.saved.mapping(id)));
        }
        sdk::DocumentGroup animations = group(QStringLiteral("Animations"));
        for (const QString& id : sequences().animationIds()) {
            const QJsonObject animation = sequences().animation(id);
            animations.documents.append(entry(QStringLiteral("animation:") + id, id,
                                              QStringLiteral("%1 fr").arg(animation.value(QStringLiteral("frames")).toArray().size()),
                                              animation != sequences_.saved.animation(id)));
        }
        sdk::DocumentGroup motion = group(QStringLiteral("Bonus motion"));
        for (const QString& id : sequences().bonusMotionIds()) {
            const QJsonObject record = sequences().bonusMotion(id);
            motion.documents.append(entry(QStringLiteral("motion:") + id, id,
                                          record.value(QStringLiteral("kind")).toString(),
                                          record != sequences_.saved.bonusMotion(id)));
        }
        groups << mappings << animations;
        if (!motion.documents.isEmpty()) {
            groups << motion;
        }
    } else if (kind == QLatin1String("text")) {
        groups.append(group(QStringLiteral("All game text"),
                            {entry(QStringLiteral("game-text"), QStringLiteral("Game text"),
                                   QStringLiteral("%1 strings").arg(semantics().textIds().size()),
                                   !sameText(semantics(), semantics_.saved))}));
    } else if (kind == QLatin1String("sound")) {
        sdk::DocumentGroup songs = group(QStringLiteral("Music & SFX"));
        for (const QString& id : sound().headerIds()) {
            bool dirty = false;
            for (const FlickySoundTrack& track : sound().header(id).tracks) {
                dirty = dirty || sound().blockText(track.sequence) != sound_.saved.blockText(track.sequence);
            }
            dirty = dirty || sound().blockText(id) != sound_.saved.blockText(id);
            songs.documents.append(entry(QStringLiteral("header:") + id, id, sound().header(id).kind, dirty));
        }
        sdk::DocumentGroup voices = group(QStringLiteral("FM voices"));
        for (const QString& id : sound().voiceBankIds()) {
            voices.documents.append(entry(QStringLiteral("voice:") + id, id,
                                          QStringLiteral("%1 voices").arg(sound().voiceCount(id)),
                                          sound().blockText(id) != sound_.saved.blockText(id)));
        }
        sdk::DocumentGroup data = group(QStringLiteral("Sound data"));
        data.documents.append(entry(QStringLiteral("envelopes"), QStringLiteral("Priority & envelopes"),
                                    QStringLiteral("%1 bytes").arg(sound().priorityEnvelopeBytes().size()),
                                    sound().priorityEnvelopeBytes() != sound_.saved.priorityEnvelopeBytes()));
        groups << songs << voices << data;
    } else if (kind == QLatin1String("playtest")) {
        groups.append(group(QString(), {entry(QStringLiteral("playtest"), QStringLiteral("Build and run"),
                                              lastBuild_.isEmpty() ? QString() : QStringLiteral("built"))}));
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
    } else if (kind == QLatin1String("tiles")) {
        if (document == QLatin1String("sega_palette")) {
            return std::make_unique<SegaPaletteModel>(this);
        }
        if (document == QLatin1String("sega_tilemap")) {
            return std::make_unique<SegaTilemapModel>(this);
        }
        if (graphics().tileAssetIds().contains(document)) {
            return std::make_unique<TileBankModel>(this, document);
        }
    } else if (kind == QLatin1String("palettes")) {
        if (semantics().paletteIds().contains(document)) {
            return std::make_unique<SemanticsPaletteModel>(this, document);
        }
    } else if (kind == QLatin1String("text")) {
        return std::make_unique<TextModel>(this);
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
    levels_ = levels;
    for (auto it = previews_.begin(); it != previews_.end();) {
        it = it.key().startsWith(QLatin1String("round:")) ? previews_.erase(it) : std::next(it);
    }
    emit levelsChanged();
    emit contentChanged();
    emit documentsChanged(QStringLiteral("levels"));
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

void FlickySession::setSemantics(const FlickySemanticsDocument& doc, quint64 revision) {
    *semantics_.doc = doc;
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
            *error = QStringLiteral("Cannot save %1: %2").arg(QFileInfo(path).fileName(),
                                                              QString::fromUtf8(e.what()));
        }
        return false;
    };
    bool levelsDirty = bonusPlacementsDirty();
    for (int layout = 0; layout < objects::kLayouts && !levelsDirty; ++layout) {
        levelsDirty = isLayoutDirty(layout);
    }
    try {
        if (levelsDirty) {
            levels_.save(levelsPath_);
        }
    } catch (const std::exception& e) {
        return failed(levelsPath_, e);
    }
    try {
        if (graphics_.isDirty()) {
            graphics_.doc->save(graphics_.path);
        }
    } catch (const std::exception& e) {
        return failed(graphics_.path, e);
    }
    try {
        if (semantics_.isDirty()) {
            semantics_.doc->save(semantics_.path);
        }
    } catch (const std::exception& e) {
        return failed(semantics_.path, e);
    }
    try {
        if (sequences_.isDirty()) {
            sequences_.doc->save(sequences_.path);
        }
    } catch (const std::exception& e) {
        return failed(sequences_.path, e);
    }
    try {
        if (sound_.isDirty()) {
            sound_.doc->save(sound_.path);
        }
    } catch (const std::exception& e) {
        return failed(sound_.path, e);
    }
    savedLevels_ = levels_;
    graphics_.saved = *graphics_.doc;
    graphics_.savedRevision = graphics_.revision;
    semantics_.saved = *semantics_.doc;
    semantics_.savedRevision = semantics_.revision;
    sequences_.saved = *sequences_.doc;
    sequences_.savedRevision = sequences_.revision;
    sound_.saved = *sound_.doc;
    sound_.savedRevision = sound_.revision;
    dirtyTilesRevision_ = ~quint64(0);
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return true;
}

bool FlickySession::canRevert(const QString& kind, const QString& document) const {
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
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        if (round < 0) {
            return Session::revert(kind, document, error);
        }
        if (objects::isBonusRound(round)) {
            return editLevels(QStringLiteral("Revert bonus placements"),
                              [](FlickyLevelDocument& levels) {
                                  for (const char* id : {"seesaw", "tiger", "chick_spawn"}) {
                                      levels.restoreBonusPlacement(QString::fromLatin1(id));
                                  }
                              },
                              error);
        }
        return editLevels(
            QStringLiteral("Revert Round %1").arg(two(round + 1)),
            [round](FlickyLevelDocument& levels) {
                levels.restoreLayout(levels.layoutForRound(round));
                for (const QString& field : FlickyLevelDocument::objectFields()) {
                    levels.restoreObjectGroup(levels.objectLayoutForRound(round, field), field);
                }
            },
            error);
    }
    if (kind == QLatin1String("tiles")) {
        return editGraphics(QStringLiteral("Revert %1").arg(document),
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
        return editSemantics(QStringLiteral("Revert %1").arg(document),
                             [document](FlickySemanticsDocument& s) { s.restorePalette(document); },
                             error);
    }
    if (kind == QLatin1String("sprites")) {
        const QString id = document.section(QLatin1Char(':'), 1);
        return editSequences(QStringLiteral("Revert %1").arg(id),
                             [document, id](FlickySequencesDocument& q) {
                                 if (document.startsWith(QLatin1String("mapping:"))) q.restoreMapping(id);
                                 else if (document.startsWith(QLatin1String("animation:"))) q.restoreAnimation(id);
                                 else q.restoreBonusMotion(id);
                             },
                             error);
    }
    if (kind == QLatin1String("sound")) {
        return editSound(QStringLiteral("Revert %1").arg(document),
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
    if (kind == QLatin1String("text")) {
        return editSemantics(QStringLiteral("Revert game text"),
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
            d.message = QStringLiteral("%1 uses %2 of %3 bytes; the ROM has no room for %4 more.")
                            .arg(what).arg(value).arg(capacity).arg(value - capacity);
        } else {
            d.severity = value * 100 >= capacity * 98 ? sdk::Severity::Warning : sdk::Severity::Info;
            d.message = QStringLiteral("%1 uses %2 of %3 bytes.").arg(what).arg(value).arg(capacity);
        }
        result.append(d);
    };
    check(QStringLiteral("Level data"), used.first, limit.first);
    check(QStringLiteral("Special tile data"), used.second, limit.second);
    return result;
}

QString FlickySession::selectedStart() const {
    return QStringLiteral("Round ") + two(playRound_ + 1);
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

void FlickySession::report(sdk::Severity severity, const QString& source, const QString& message) {
    if (!host()) {
        return;
    }
    sdk::Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = message;
    d.location = QFileInfo(project_.path()).fileName();
    host()->report(d);
}

namespace {

// The last line of stderr usually says why a kit command failed.
QString failureText(const sdk::ProcessResult& result) {
    if (!result.started) {
        return result.error;
    }
    const QStringList lines = QString::fromUtf8(result.standardError).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (!lines.at(i).trimmed().isEmpty()) {
            return lines.at(i).trimmed();
        }
    }
    return QStringLiteral("exit code %1").arg(result.exitCode);
}

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

void FlickySession::runBuild(std::function<void(bool)> next, bool force) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) {
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
    const QString writer = h->toolPath(QStringLiteral("retrolarium-flicky-level-writer"));
    if (writer.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("build"),
               QStringLiteral("retrolarium-flicky-level-writer is missing next to the studio."));
        next(false);
        return;
    }
    const QByteArray inputs = buildInputs(writer);
    if (!force && inputs == builtInputs_ && QFileInfo::exists(builtRom_)) {
        report(sdk::Severity::Info, QStringLiteral("build"), QStringLiteral("The ROM is up to date."));
        next(true);
        return;
    }
    QString error;
    sdk::ProcessSpec spec = h->kitCommand(
        {QStringLiteral("studio"), QStringLiteral("build"), project_.path(), QStringLiteral("--flicky-level-writer"),
         writer},
        project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("build"), error);
        next(false);
        return;
    }
    spec.label = QStringLiteral("Build ROM");
    spec.environment.insert(QStringLiteral("RETROLARIUM_FLICKY_LEVEL_WRITER"), writer);
    h->runProcess(spec, [this, next, inputs](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, QStringLiteral("build"),
                       QStringLiteral("Build failed: %1").arg(failureText(result)));
            }
            next(false);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        const QJsonObject artifact = json.value(QStringLiteral("artifact")).toObject();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
            report(sdk::Severity::Error, QStringLiteral("build"),
                   QStringLiteral("The kit did not report a built ROM."));
            next(false);
            return;
        }
        builtInputs_ = inputs;
        builtRom_ = QDir(project_.directory()).absoluteFilePath(artifact.value(QStringLiteral("path")).toString());
        plans_.clear();
        saveBuildRecord();
        lastBuild_ = QStringLiteral("%1 · %2")
                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
                              artifact.value(QStringLiteral("path")).toString());
        report(sdk::Severity::Info, QStringLiteral("build"),
               QStringLiteral("Built %1 (%2 bytes, sha256 %3…).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt())
                   .arg(artifact.value(QStringLiteral("sha256")).toString().left(12)));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void FlickySession::build() {
    // An explicit Build always runs the kit.
    runBuild([](bool) {}, true);
}

void FlickySession::play() {
    launch(-1);
}

void FlickySession::playSelected() {
    launch(playRound_);
}

void FlickySession::launch(int round) {
    const bool fromRound = round >= 0;
    runBuild([this, round, fromRound](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) {
            return;
        }
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   QStringLiteral("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        const QString key = (fromRound ? QStringLiteral("round:%1").arg(round) : QStringLiteral("power-on")) +
                            QLatin1Char('|') + mame;
        const auto planned = plans_.constFind(key);
        if (planned != plans_.constEnd()) {
            launchPlanned(*planned, round);
            return;
        }
        QString error;
        const QStringList arguments =
            fromRound ? QStringList{QStringLiteral("studio"), QStringLiteral("flicky-round-playtest-plan"),
                                    project_.path(), QStringLiteral("--mame"), mame, QStringLiteral("--round"),
                                    QString::number(round + 1)}
                      : QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path(),
                                    QStringLiteral("--mame"), mame};
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        plan.label = fromRound ? QStringLiteral("Plan playtest of Round %1").arg(two(round + 1))
                               : QStringLiteral("Plan playtest from power-on");
        h->runProcess(plan, [this, round, key](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) return;
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           QStringLiteral("Playtest plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
            sdk::ProcessSpec game;
            game.program = json.value(QStringLiteral("program")).toString();
            for (const QJsonValue& argument : json.value(QStringLiteral("arguments")).toArray()) {
                game.arguments << argument.toString();
            }
            game.workingDirectory = json.value(QStringLiteral("working_directory")).toString();
            const QJsonObject environment = json.value(QStringLiteral("environment")).toObject();
            for (auto it = environment.constBegin(); it != environment.constEnd(); ++it) {
                game.environment.insert(it.key(), it.value().toString());
            }
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"),
                       QStringLiteral("The kit returned no emulator command."));
                return;
            }
            plans_.insert(key, game);
            saveBuildRecord();
            launchPlanned(game, round);
        });
    });
}

void FlickySession::launchPlanned(const sdk::ProcessSpec& planned, int round) {
    sdk::Host* host = this->host();
    if (!host) {
        return;
    }
    sdk::ProcessSpec game = planned;
    const bool fromRound = round >= 0;
    if (fromRound) {
        // The kit's playtest script writes one status line here once
        // the round is on screen (or could not be reached).
        game.readyFile =
            QDir(project_.directory()).filePath(QStringLiteral("build/playtest-status.txt"));
        game.environment.insert(QStringLiteral("FLICKY_PLAYTEST_RESULT"), game.readyFile);
    }
    const QString title = fromRound ? QStringLiteral("Flicky · Round %1").arg(two(round + 1))
                                    : QStringLiteral("Flicky · power-on");
    host->launchEmulator(game, title,
                         [this](const sdk::ProcessResult& run) {
                             if (run.crashed || (!run.started && !run.error.isEmpty())) {
                                 report(sdk::Severity::Error, QStringLiteral("playtest"),
                                        run.error.isEmpty() ? QStringLiteral("The emulator crashed.")
                                                            : run.error);
                             }
                         });
}

}  // namespace rl::flicky
