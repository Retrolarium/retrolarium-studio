#include "smb/plugin/smb_session.hpp"

#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"
#include "smb/plugin/graphics_models.hpp"
#include "smb/plugin/level_model.hpp"
#include "smb/plugin/metatile_model.hpp"
#include "smb/plugin/player_models.hpp"
#include "smb/plugin/playtest_model.hpp"
#include "smb/plugin/screen_model.hpp"
#include "smb/plugin/text_model.hpp"
#include "smb/plugin/world_models.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoStack>

namespace rl::smb {

namespace {

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

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta = QString()) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    return result;
}

// "ground_12" -> "Ground 12"
QString areaLabel(const QString& name) {
    QString type = areaTypeOf(name);
    if (!type.isEmpty()) {
        type[0] = type.at(0).toUpper();
    }
    return QStringLiteral("%1 %2").arg(type, name.section(QLatin1Char('_'), -1));
}

// "day_snow" -> "Day snow"
QString displayName(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!name.isEmpty()) {
        name[0] = name.at(0).toUpper();
    }
    return name;
}

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

// The emulator command a kit plan describes.
sdk::ProcessSpec specFromPlan(const QJsonObject& json) {
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
    return game;
}

}  // namespace

// ---------------------------------------------------------------- opening

std::unique_ptr<SmbSession> SmbSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<SmbSession> session(new SmbSession);
    session->project_ = project;
    try {
        const QString relative = tabDocument(project, QStringLiteral("smb.levels.v1"));
        if (relative.isEmpty()) {
            throw SmbError("The project has no enabled levels tab (smb.levels.v1).");
        }
        session->documentPath_ = project.resolve(relative);
        session->document_ = SmbResourceDocument::load(session->documentPath_);
        session->saved_ = session->document_;
        const SmbResourceDocument& d = session->document_;
        const auto first = [&d](const char* codec) {
            const QStringList ids = d.resourceIds(QString::fromLatin1(codec));
            return ids.isEmpty() ? QString() : ids.first();
        };
        session->levels_ = first("smb.levels.v1");
        session->world_ = first("smb.world.v1");
        session->art_.chr = first("nes.chr-2bpp.v1");
        session->art_.metatiles = first("nes.metatile-2x2.v1");
        session->art_.palettes = first("nes.ppu-palette-packets.v1");
        session->art_.playerFrames = first("smb.player-frames.v1");
        session->art_.playerPalette = first("nes.palette-table.v1");
        if (session->levels_.isEmpty() || session->art_.chr.isEmpty() || session->art_.metatiles.isEmpty() ||
            session->art_.palettes.isEmpty()) {
            throw SmbError("The resource bundle lacks levels, pattern tiles, metatiles or palettes.");
        }
        session->painter_ = std::make_unique<SmbLevelPainter>(session->document_, session->art_);
        const QStringList areas = session->document_.areaNames(session->levels_);
        if (!areas.isEmpty()) {
            // Start where the game does: the area of world 1, course 1.
            QString opening = areas.first();
            for (const QString& area : areas) {
                if (session->courseOf(area) == qMakePair(1, 1)) {
                    opening = area;
                    break;
                }
            }
            session->selectStart(opening);
        }
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("Cannot open the Super Mario Bros. project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile SmbSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Super Mario Bros.");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    profile.revision = document_.revision().toUpper();
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("smb.levels.v1");
    return profile;
}

QVector<sdk::Kind> SmbSession::kinds() const {
    if (world_.isEmpty()) {
        return {kind("levels", "Levels", "levels", sdk::EditorType::TileMap),
                kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
                kind("metatiles", "Metatiles", "metatiles", sdk::EditorType::TileMap),
                kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
                kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
                kind("text", "Text", "text", sdk::EditorType::Table),
                kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
    }
    return {kind("levels", "Levels", "levels", sdk::EditorType::TileMap),
            kind("world", "World", "world", sdk::EditorType::Table),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("metatiles", "Metatiles", "metatiles", sdk::EditorType::TileMap),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("player", "Player", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> SmbSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("levels")) {
        const QStringList order{QStringLiteral("ground"), QStringLiteral("underground"), QStringLiteral("water"),
                                QStringLiteral("castle")};
        const QStringList areas = document_.areaNames(levels_);
        for (const QString& type : order) {
            sdk::DocumentGroup group;
            group.title = displayName(type) + QStringLiteral(" areas");
            for (const QString& area : areas) {
                if (areaTypeOf(area) != type) {
                    continue;
                }
                const QPair<int, int> course = levelOf(area);
                sdk::DocumentEntry e = entry(area, areaLabel(area),
                                             course.first > 0 ? QStringLiteral("%1-%2").arg(course.first).arg(course.second)
                                                              : QString());
                e.dirty = document_.area(levels_, area).objects != saved_.area(levels_, area).objects ||
                          document_.area(levels_, area).enemies != saved_.area(levels_, area).enemies ||
                          document_.area(levels_, area).header != saved_.area(levels_, area).header;
                group.documents << e;
            }
            if (!group.documents.isEmpty()) {
                groups << group;
            }
        }
    } else if (kind == QLatin1String("graphics")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Pattern tables");
        group.documents << entry(QStringLiteral("sprites"), QStringLiteral("Sprites"), QStringLiteral("256 tiles"))
                        << entry(QStringLiteral("background"), QStringLiteral("Background"), QStringLiteral("256 tiles"));
        groups << group;
    } else if (kind == QLatin1String("metatiles")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Background blocks");
        const QVector<int> sizes = document_.metatileGroupSizes(art_.metatiles);
        const QJsonArray now = document_.resource(art_.metatiles).value(QStringLiteral("records")).toArray();
        const QJsonArray saved = saved_.resource(art_.metatiles).value(QStringLiteral("records")).toArray();
        int first = 0;
        for (int g = 0; g < sizes.size(); ++g) {
            sdk::DocumentEntry e = entry(QStringLiteral("group:%1").arg(g), QStringLiteral("Palette %1").arg(g),
                                         QStringLiteral("%1 blocks").arg(sizes.at(g)));
            for (int i = first; i < first + sizes.at(g) && i < now.size(); ++i) {
                e.dirty = e.dirty || now.at(i) != saved.at(i);
            }
            first += sizes.at(g);
            group.documents << e;
        }
        groups << group;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup areas;
        areas.title = QStringLiteral("Area palettes");
        for (const QString& name : document_.packetNames(art_.palettes)) {
            areas.documents << entry(QStringLiteral("packet:") + name, displayName(name),
                                     QStringLiteral("%1 colours").arg(document_.packetValues(art_.palettes, name).size()));
        }
        groups << areas;
        if (!art_.playerPalette.isEmpty()) {
            sdk::DocumentGroup player;
            player.title = QStringLiteral("Player");
            player.documents << entry(QStringLiteral("player"), QStringLiteral("Hero, Brother, fire"),
                                      QStringLiteral("%1 colours").arg(document_.tableValues(art_.playerPalette).size()));
            groups << player;
        }
    } else if (kind == QLatin1String("world") && !world_.isEmpty()) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("World");
        int steps = 0;
        bool routesDirty = false;
        for (int w = 1; w <= document_.worldCount(world_); ++w) {
            const QVector<SmbRoute> now = document_.routes(world_, w);
            const QVector<SmbRoute> saved = saved_.routes(world_, w);
            steps += now.size();
            for (int s = 0; s < now.size() && s < saved.size(); ++s) {
                routesDirty = routesDirty || now.at(s).areaType != saved.at(s).areaType ||
                              now.at(s).areaIndex != saved.at(s).areaIndex || now.at(s).alternate != saved.at(s).alternate;
            }
        }
        sdk::DocumentEntry routes = entry(QStringLiteral("routes"), QStringLiteral("Routes"),
                                          QStringLiteral("%1 worlds · %2 steps").arg(document_.worldCount(world_)).arg(steps));
        routes.dirty = routesDirty;
        sdk::DocumentEntry physics = entry(QStringLiteral("physics"), QStringLiteral("Player physics"),
                                           QStringLiteral("%1 tables").arg(document_.physicsTables(world_).size()));
        const QVector<SmbPhysicsTable> nowTables = document_.physicsTables(world_);
        const QVector<SmbPhysicsTable> savedTables = saved_.physicsTables(world_);
        for (int t = 0; t < nowTables.size() && t < savedTables.size(); ++t) {
            physics.dirty = physics.dirty || nowTables.at(t).values != savedTables.at(t).values;
        }
        group.documents << routes << physics;
        if (!art_.playerFrames.isEmpty()) {
            sdk::DocumentEntry actions = entry(QStringLiteral("actions"), QStringLiteral("Player actions"),
                                               QStringLiteral("frames per action"));
            const QJsonObject now = document_.resource(art_.playerFrames);
            const QJsonObject saved = saved_.resource(art_.playerFrames);
            actions.dirty = now.value(QStringLiteral("frame_offsets")) != saved.value(QStringLiteral("frame_offsets")) ||
                            now.value(QStringLiteral("swim_kick_tiles")) != saved.value(QStringLiteral("swim_kick_tiles"));
            group.documents << actions;
        }
        groups << group;
    } else if (kind == QLatin1String("player") && !art_.playerFrames.isEmpty()) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Player");
        sdk::DocumentEntry frames = entry(QStringLiteral("frames"), QStringLiteral("Hero frames"),
                                          QStringLiteral("%1 frames").arg(document_.playerFrameCount(art_.playerFrames)));
        frames.dirty = document_.resource(art_.playerFrames).value(QStringLiteral("frames")) !=
                       saved_.resource(art_.playerFrames).value(QStringLiteral("frames"));
        group.documents << frames;
        groups << group;
    } else if (kind == QLatin1String("screens")) {
        for (const QString& id : document_.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1"))) {
            const SmbScreenSet& set = screenSet(id);
            sdk::DocumentGroup group;
            group.title = id == QLatin1String("game-screen-packets") ? QStringLiteral("Game screens")
                                                                     : displayName(QString(id).replace(QLatin1Char('-'), QLatin1Char('_')));
            const SmbScreenSet saved = SmbScreenSet::fromJson(saved_.resource(id));
            for (int s = 0; s < set.screens.size(); ++s) {
                const SmbScreen& screen = set.screens.at(s);
                sdk::DocumentEntry e = entry(id + QLatin1Char('/') + screen.key, screen.label,
                                             QStringLiteral("%1 B").arg(screen.capacity));
                e.dirty = s < saved.screens.size() && (saved.screens.at(s).tiles != screen.tiles ||
                                                       saved.screens.at(s).attributes != screen.attributes);
                group.documents << e;
            }
            if (!group.documents.isEmpty()) {
                groups << group;
            }
        }
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        sdk::DocumentEntry e = entry(QStringLiteral("all"), QStringLiteral("Game text"),
                                     QStringLiteral("%1 strings").arg(textSlots().size()));
        for (int i = 0; i < textSlots().size(); ++i) {
            e.dirty = e.dirty || text(i) != slotText(saved_, textSlots().at(i));
        }
        group.documents << e;
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

std::unique_ptr<sdk::EditorModel> SmbSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("levels") && document_.areaNames(levels_).contains(document)) {
            selectStart(document);
            return std::make_unique<SmbLevelModel>(this, document);
        }
        if (kind == QLatin1String("graphics") &&
            (document == QLatin1String("sprites") || document == QLatin1String("background"))) {
            return std::make_unique<SmbTileModel>(this, document == QLatin1String("background"));
        }
        if (kind == QLatin1String("player") && document == QLatin1String("frames") && !art_.playerFrames.isEmpty() &&
            !art_.playerPalette.isEmpty()) {
            return std::make_unique<SmbPlayerFramesModel>(this);
        }
        if (kind == QLatin1String("metatiles") && document.startsWith(QLatin1String("group:"))) {
            const int group = document.mid(6).toInt();
            if (group >= 0 && group < document_.metatileGroupSizes(art_.metatiles).size()) {
                return std::make_unique<SmbMetatileModel>(this, group);
            }
        }
        if (kind == QLatin1String("palettes")) {
            if (document.startsWith(QLatin1String("packet:")) &&
                document_.packetNames(art_.palettes).contains(document.mid(7))) {
                return std::make_unique<SmbPacketModel>(this, document.mid(7));
            }
            if (document == QLatin1String("player") && !art_.playerPalette.isEmpty()) {
                return std::make_unique<SmbPlayerPaletteModel>(this);
            }
        }
        if (kind == QLatin1String("world") && !world_.isEmpty()) {
            if (document == QLatin1String("routes")) {
                return std::make_unique<SmbRoutesModel>(this);
            }
            if (document == QLatin1String("physics")) {
                return std::make_unique<SmbPhysicsModel>(this);
            }
            if (document == QLatin1String("actions") && !art_.playerFrames.isEmpty()) {
                return std::make_unique<SmbPlayerActionsModel>(this);
            }
        }
        if (kind == QLatin1String("screens")) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const QString key = document.section(QLatin1Char('/'), 1);
            if (document_.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1")).contains(id) &&
                screenSet(id).indexOf(key) >= 0) {
                return std::make_unique<SmbScreenModel>(this, id, key);
            }
        }
        if (kind == QLatin1String("text") && document == QLatin1String("all")) {
            return std::make_unique<SmbTextModel>(this);
        }
        if (kind == QLatin1String("playtest")) {
            return std::make_unique<SmbPlaytestModel>(this);
        }
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

void SmbSession::setDocument(const SmbResourceDocument& document, quint64 revision) {
    document_ = document;
    revision_ = revision;
    painter_->clearCache();
    emit documentChanged();
    emit contentChanged();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    emit diagnosticsChanged();
}

bool SmbSession::edit(const QString& text, const std::function<void(SmbResourceDocument&)>& change,
                      QString* error) {
    SmbResourceDocument next = document_;
    try {
        change(next);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    const SmbResourceDocument before = document_;
    const quint64 beforeRevision = revision_;
    const quint64 afterRevision = nextRevision_++;
    undoStack()->push(new sdk::SnapshotCommand<SmbResourceDocument>(
        [this](const SmbResourceDocument& doc, quint64 revision) { setDocument(doc, revision); }, before,
        beforeRevision, next, afterRevision, text));
    return true;
}

void SmbSession::setLive(const SmbResourceDocument& document) {
    setDocument(document, nextRevision_++);
}

void SmbSession::commit(const SmbResourceDocument& before, quint64 beforeRevision, const QString& text) {
    // The live state is already shown; the command's first redo re-applies it.
    undoStack()->push(new sdk::SnapshotCommand<SmbResourceDocument>(
        [this](const SmbResourceDocument& doc, quint64 revision) { setDocument(doc, revision); }, before,
        beforeRevision, document_, revision_, text));
}

bool SmbSession::save(QString* error) {
    try {
        if (revision_ != savedRevision_) {
            document_.save(documentPath_);
        }
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    saved_ = document_;
    savedRevision_ = revision_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return true;
}

QVector<sdk::Diagnostic> SmbSession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    for (const QString& name : document_.areaNames(levels_)) {
        const SmbArea area = document_.area(levels_, name);
        const auto check = [&](const QString& stream, bool fits, int used, int capacity) {
            if (fits) {
                return;
            }
            sdk::Diagnostic d;
            d.severity = sdk::Severity::Error;
            d.source = QStringLiteral("levels");
            d.kind = QStringLiteral("levels");
            d.document = name;
            d.location = areaLabel(name);
            d.message = QStringLiteral("The %1 stream uses %2 of %3 bytes.").arg(stream).arg(used).arg(capacity);
            result << d;
        };
        check(QStringLiteral("object"), area.areaFits(), area.areaBytes(), area.areaCapacity);
        check(QStringLiteral("enemy"), area.enemiesFit(), area.enemyBytes(), area.enemyCapacity);
    }
    return result;
}

void SmbSession::report(sdk::Severity severity, const QString& source, const QString& message) {
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

// ---------------------------------------------------------------- screens and text

const SmbScreenSet& SmbSession::screenSet(const QString& resource) const {
    if (screenRevision_ != revision_) {
        screenSets_.clear();
        screenRevision_ = revision_;
    }
    auto found = screenSets_.find(resource);
    if (found == screenSets_.end()) {
        found = screenSets_.insert(resource, SmbScreenSet::fromJson(document_.resource(resource)));
    }
    return *found;
}

const QVector<SmbTextSlot>& SmbSession::textSlots() const {
    if (textRevision_ != revision_) {
        textSlots_ = smb::textSlots(document_);
        texts_.clear();
        for (const SmbTextSlot& slot : textSlots_) {
            texts_ << slotText(document_, slot);
        }
        textRevision_ = revision_;
    }
    return textSlots_;
}

QString SmbSession::text(int slot) const {
    textSlots();
    return texts_.value(slot);
}

// ---------------------------------------------------------------- start point

QPair<int, int> SmbSession::courseOf(const QString& area) const {
    if (world_.isEmpty()) {
        return {0, 0};
    }
    const int type = document_.areaTypes(levels_).indexOf(areaTypeOf(area));
    const int index = area.section(QLatin1Char('_'), -1).toInt() - 1;
    for (int w = 1; w <= document_.worldCount(world_); ++w) {
        const QVector<SmbRoute> routes = document_.routes(world_, w);
        for (int c = 0; c < routes.size(); ++c) {
            if (routes.at(c).areaType == type && routes.at(c).areaIndex == index) {
                return {w, c + 1};
            }
        }
    }
    return {0, 0};
}

QPair<int, int> SmbSession::levelOf(const QString& area) const {
    const QPair<int, int> course = courseOf(area);
    if (course.first <= 0) {
        return course;
    }
    const QStringList types = document_.areaTypes(levels_);
    const QVector<SmbRoute> routes = document_.routes(world_, course.first);
    int level = 1;
    for (int c = 0; c + 1 < course.second && c < routes.size(); ++c) {
        const QString name = QStringLiteral("%1_%2").arg(types.value(routes.at(c).areaType)).arg(routes.at(c).areaIndex + 1);
        const int entrance = document_.area(levels_, name).header.value(1);
        if (entrance != 6 && entrance != 7) {
            ++level;
        }
    }
    return {course.first, level};
}

QPoint SmbSession::heroCell(const QString& area) const {
    const auto found = hero_.constFind(area);
    return found != hero_.constEnd() ? *found : entranceCell(document_.area(levels_, area).header);
}

void SmbSession::setHeroCell(const QString& area, QPoint cell) {
    hero_.insert(area, cell);
    if (start_.area == area) {
        start_.cell = cell;
        emit startChanged();
        emit selectedStartChanged();
    }
}

void SmbSession::selectStart(const QString& area) {
    const QPair<int, int> course = courseOf(area);
    start_.area = area;
    start_.cell = heroCell(area);
    // An area no route plays still starts: the kit loads it directly.
    start_.world = course.first > 0 ? course.first : 1;
    start_.course = course.first > 0 ? course.second : 1;
    start_.level = course.first > 0 ? levelOf(area).second : 1;
    emit startChanged();
    emit selectedStartChanged();
}

QString SmbSession::selectedStart() const {
    if (start_.area.isEmpty()) {
        return {};
    }
    return QStringLiteral("%1 · column %2").arg(areaLabel(start_.area)).arg(start_.cell.x());
}

QImage SmbSession::areaPreview(const QString& area, int columns) const {
    const SmbArea a = document_.area(levels_, area);
    const SmbLevelScene scene =
        renderSmbLevelScene(area, a.header, a.objects, a.enemies, std::max(0, courseOf(area).first - 1),
                            SmbLevelRendering::fromId(document_.rendering(levels_)));
    const QImage image = painter_->render(scene, areaTypeOf(area), night(area));
    return image.copy(0, 0, std::min(image.width(), columns * SmbLevelPainter::kCell), image.height());
}

bool SmbSession::night(const QString& area) const {
    if (lighting_ == QLatin1String("Day")) return false;
    if (lighting_ == QLatin1String("Night")) return true;
    return automaticNight(areaTypeOf(area), document_.area(levels_, area).header);
}

void SmbSession::setLighting(const QString& lighting) {
    if (lighting != lighting_) {
        lighting_ = lighting;
        emit contentChanged();
    }
}

// ---------------------------------------------------------------- build and play

void SmbSession::runBuild(std::function<void(bool)> next, bool) {
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
                report(sdk::Severity::Error, QStringLiteral("build"),
                       QStringLiteral("Build failed: %1").arg(failureText(result)));
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
        lastBuild_ = QStringLiteral("%1 · %2")
                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
                              artifact.value(QStringLiteral("path")).toString());
        report(sdk::Severity::Info, QStringLiteral("build"),
               QStringLiteral("Built %1 (%2 bytes).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt()));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void SmbSession::build() {
    runBuild([](bool) {}, true);
}

void SmbSession::play() {
    launch(true);
}

void SmbSession::playSelected() {
    launch(false);
}

void SmbSession::launch(bool fromStart) {
    const SmbStart start = start_;
    runBuild(
        [this, fromStart, start](bool ok) {
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
            QStringList arguments;
            if (fromStart) {
                arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path(),
                             QStringLiteral("--mame"), mame};
            } else {
                arguments = QStringList{QStringLiteral("studio"), QStringLiteral("smb-point-playtest-plan"), project_.path(),
                             QStringLiteral("--mame"), mame, QStringLiteral("--course-bank"), levels_,
                             QStringLiteral("--area"), start.area, QStringLiteral("--world"), QString::number(start.world),
                             QStringLiteral("--course"), QString::number(start.course), QStringLiteral("--column"),
                             QString::number(start.cell.x()), QStringLiteral("--row"), QString::number(start.cell.y()),
                             QStringLiteral("--theme"),
                             night(start.area) ? QStringLiteral("night") : QStringLiteral("day"),
                             QStringLiteral("--sfx-channel"), QStringLiteral("none"), QStringLiteral("--sfx-value"),
                             QStringLiteral("0")};
            }
            QString error;
            sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
            if (plan.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), error);
                return;
            }
            const QString title = fromStart ? QStringLiteral("Super Mario Bros. · power-on")
                                            : QStringLiteral("Super Mario Bros. · %1").arg(areaLabel(start.area));
            plan.label = fromStart ? QStringLiteral("Plan playtest from power-on")
                                   : QStringLiteral("Plan playtest of %1").arg(areaLabel(start.area));
            h->runProcess(plan, [this, title](const sdk::ProcessResult& result) {
                sdk::Host* host = this->host();
                if (!host) {
                    return;
                }
                if (!result.succeeded()) {
                    if (!result.cancelled) {
                        report(sdk::Severity::Error, QStringLiteral("playtest"),
                               QStringLiteral("Playtest plan failed: %1").arg(failureText(result)));
                    }
                    return;
                }
                const sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
                if (game.program.isEmpty()) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           QStringLiteral("The kit returned no emulator command."));
                    return;
                }
                host->launchEmulator(game, title, [this](const sdk::ProcessResult& run) {
                    if (run.crashed || (!run.started && !run.error.isEmpty())) {
                        report(sdk::Severity::Error, QStringLiteral("playtest"),
                               run.error.isEmpty() ? QStringLiteral("The emulator crashed.") : run.error);
                    }
                });
            });
        },
        false);
}

}  // namespace rl::smb
