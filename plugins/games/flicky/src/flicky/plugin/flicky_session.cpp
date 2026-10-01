#include "flicky/plugin/flicky_session.hpp"

#include "flicky/plugin/graphics_models.hpp"
#include "flicky/plugin/levels_model.hpp"
#include "flicky/plugin/objects.hpp"
#include "flicky/plugin/semantics_models.hpp"
#include "megadrive/vdp.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUndoCommand>
#include <QUndoStack>

namespace rl::flicky {

namespace {

// One edit: swaps between two whole-document states. Documents keep their
// JSON implicitly shared, so a snapshot costs little until it is changed, and
// every edit, including multi-field ones, is exactly reversible.
template <class T>
class SnapshotCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(const T&, quint64)>;

    SnapshotCommand(Apply apply, T before, quint64 beforeRevision, T after, quint64 afterRevision,
                    const QString& text)
        : QUndoCommand(text),
          apply_(std::move(apply)),
          before_(std::move(before)),
          after_(std::move(after)),
          beforeRevision_(beforeRevision),
          afterRevision_(afterRevision) {}

    void redo() override { apply_(after_, afterRevision_); }
    void undo() override { apply_(before_, beforeRevision_); }

private:
    Apply apply_;
    T before_;
    T after_;
    quint64 beforeRevision_;
    quint64 afterRevision_;
};

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
        session->sound_ =
            std::make_unique<FlickySoundDocument>(FlickySoundDocument::load(require(QStringLiteral("flicky.sound"))));
        session->renderer_ = std::make_unique<FlickyLevelSceneRenderer>(
            session->sourceRoot_, session->graphics_.doc, session->semantics_.doc,
            session->sequences_.doc);
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
            mappings.documents.append(entry(QStringLiteral("mapping:") + id, id, QString()));
        }
        sdk::DocumentGroup animations = group(QStringLiteral("Animations"));
        for (const QString& id : sequences().animationIds()) {
            animations.documents.append(entry(QStringLiteral("animation:") + id, id, QString()));
        }
        groups << mappings << animations;
    } else if (kind == QLatin1String("text")) {
        groups.append(group(QStringLiteral("All game text"),
                            {entry(QStringLiteral("game-text"), QStringLiteral("Game text"),
                                   QStringLiteral("%1 strings").arg(semantics().textIds().size()),
                                   !sameText(semantics(), semantics_.saved))}));
    } else if (kind == QLatin1String("sound")) {
        sdk::DocumentGroup songs = group(QStringLiteral("Music & SFX"));
        for (const QString& id : sound_->headerIds()) {
            songs.documents.append(entry(QStringLiteral("header:") + id, id, sound_->header(id).kind));
        }
        sdk::DocumentGroup voices = group(QStringLiteral("FM voices"));
        for (const QString& id : sound_->voiceBankIds()) {
            voices.documents.append(entry(QStringLiteral("voice:") + id, id, QString()));
        }
        groups << songs << voices;
    } else if (kind == QLatin1String("playtest")) {
        groups.append(group(QString(), {entry(QStringLiteral("playtest"), QStringLiteral("Build and run"),
                                              QString())}));
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> FlickySession::openEditor(const QString& kind,
                                                            const QString& document) {
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        if (round >= 0) {
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

bool FlickySession::isRoundDirty(int round) const {
    if (objects::isBonusRound(round)) {
        return false;
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
    bool levelsDirty = false;
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
    savedLevels_ = levels_;
    graphics_.saved = *graphics_.doc;
    graphics_.savedRevision = graphics_.revision;
    semantics_.saved = *semantics_.doc;
    semantics_.savedRevision = semantics_.revision;
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
    return false;
}

bool FlickySession::revert(const QString& kind, const QString& document, QString* error) {
    if (kind == QLatin1String("levels")) {
        const int round = roundFromId(document);
        if (round < 0) {
            return Session::revert(kind, document, error);
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

QString FlickySession::playLabel(const QString& kind, const QString& document) const {
    const int round = kind == QLatin1String("levels") ? roundFromId(document) : -1;
    return round >= 0 ? QStringLiteral("Round ") + two(round + 1) : QString();
}

}  // namespace rl::flicky
