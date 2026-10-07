#include "smb/plugin/smb_session.hpp"

#include "nes/editors.hpp"
#include "nes/palette.hpp"
#include "nes/sound_effect_model.hpp"
#include "sdk/i18n.hpp"
#include "smb/plugin/sound_effects.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"
#include "smb/plugin/demo_model.hpp"
#include "smb/plugin/graphics_models.hpp"
#include "smb/plugin/level_model.hpp"
#include "smb/plugin/metatile_model.hpp"
#include "smb/plugin/music_model.hpp"
#include "smb/plugin/player_models.hpp"
#include "smb/plugin/playtest_model.hpp"
#include "smb/plugin/screen_model.hpp"
#include "smb/plugin/text_model.hpp"
#include "smb/plugin/world_models.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QUndoStack>

namespace rl::smb {

namespace {

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
        for (const QString& id : d.resourceIds(QStringLiteral("smb.levels.v1"))) {
            const QString world = d.resource(id).value(QStringLiteral("world_resource")).toString();
            session->banks_ << CourseBank{id, world.isEmpty() ? session->world_ : world};
        }
        if (!session->banks_.isEmpty()) session->world_ = session->banks_.first().world;
        session->music_ = first("smb.apu-music.v1");
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
        const QString demo = tabDocument(project, QStringLiteral("smb.demo@1"));
        if (!demo.isEmpty()) {
            session->demoPath_ = project.resolve(demo);
            session->demo_ = loadJsonObject(session->demoPath_);
            session->savedDemo_ = session->demo_;
        }
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
            *error = tr("Cannot open the Super Mario Bros. project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile SmbSession::profile() const {
    // The kit's revision ids, as the version chip shows them.
    static const QHash<QString, QString> kRevisions{
        {QStringLiteral("ju"), QStringLiteral("JU")},        {QStringLiteral("pal"), QStringLiteral("E")},
        {QStringLiteral("pc10"), QStringLiteral("PC10")},    {QStringLiteral("vs_smb"), QStringLiteral("VS")},
        {QStringLiteral("fds_smb"), QStringLiteral("FDS")},  {QStringLiteral("ann_fds"), QStringLiteral("ANN")},
        {QStringLiteral("smb2_jp_fds"), QStringLiteral("J")},
    };
    const QString revision = document_.revision();
    sdk::Profile profile;
    profile.name = revision == QLatin1String("smb2_jp_fds") ? tr("Super Mario Bros. 2")
                                                             : tr("Super Mario Bros.");
    profile.badge = revision.endsWith(QLatin1String("fds")) ? QStringLiteral("FDS") : QStringLiteral("NES");
    profile.platform = profile.badge;
    profile.revision = kRevisions.value(revision, revision.toUpper());
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
            kind("animations", "Animations", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sound", "Sound", "sound", sdk::EditorType::Sequencer),
            kind("demo", "Demo", "play", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> SmbSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("levels")) {
        const QStringList order{QStringLiteral("ground"), QStringLiteral("underground"), QStringLiteral("water"),
                                QStringLiteral("castle")};
        for (const CourseBank& bank : banks_) {
            const QStringList areas = document_.areaNames(bank.levels);
            if (multipleBanks()) {
                // The bank heads its area groups.
                sdk::DocumentGroup heading;
                heading.title = bankLabel(bank.levels);
                groups << heading;
            }
            for (const QString& type : order) {
                sdk::DocumentGroup group;
                group.title = tr("%1 areas").arg(displayName(type));
                for (const QString& area : areas) {
                    if (areaTypeOf(area) != type) {
                        continue;
                    }
                    const QPair<int, int> course = levelOf(area, bank.levels, bank.world);
                    sdk::DocumentEntry e =
                        entry(bankDocument(bank.levels, area), areaLabel(area),
                              course.first > 0 ? QStringLiteral("%1-%2").arg(course.first).arg(course.second) : QString());
                    const SmbArea now = document_.area(bank.levels, area);
                    const SmbArea saved = saved_.area(bank.levels, area);
                    e.dirty = now.objects != saved.objects || now.enemies != saved.enemies || now.header != saved.header;
                    group.documents << e;
                }
                if (!group.documents.isEmpty()) {
                    groups << group;
                }
            }
        }
    } else if (kind == QLatin1String("graphics")) {
        sdk::DocumentGroup group;
        group.title = tr("Pattern tables");
        group.documents << entry(QStringLiteral("sprites"), tr("Sprites"), tr("256 tiles"))
                        << entry(QStringLiteral("background"), tr("Background"), tr("256 tiles"));
        groups << group;
    } else if (kind == QLatin1String("metatiles")) {
        sdk::DocumentGroup group;
        group.title = tr("Background blocks");
        const QVector<int> sizes = document_.metatileGroupSizes(art_.metatiles);
        const QJsonArray now = document_.resource(art_.metatiles).value(QStringLiteral("records")).toArray();
        const QJsonArray saved = saved_.resource(art_.metatiles).value(QStringLiteral("records")).toArray();
        int first = 0;
        for (int g = 0; g < sizes.size(); ++g) {
            sdk::DocumentEntry e = entry(QStringLiteral("group:%1").arg(g), tr("Palette %1").arg(g),
                                         tr("%n blocks", nullptr, sizes.at(g)));
            for (int i = first; i < first + sizes.at(g) && i < now.size(); ++i) {
                e.dirty = e.dirty || now.at(i) != saved.at(i);
            }
            first += sizes.at(g);
            group.documents << e;
        }
        groups << group;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup areas;
        areas.title = tr("Area palettes");
        for (const QString& name : document_.packetNames(art_.palettes)) {
            areas.documents << entry(QStringLiteral("packet:") + name, displayName(name),
                                     tr("%1 colours").arg(document_.packetValues(art_.palettes, name).size()));
        }
        groups << areas;
        if (!art_.playerPalette.isEmpty()) {
            sdk::DocumentGroup player;
            player.title = QStringLiteral("Player");
            player.documents << entry(QStringLiteral("player"), tr("Hero, Brother, fire"),
                                      tr("%n colours", nullptr, document_.tableValues(art_.playerPalette).size()));
            groups << player;
        }
    } else if (kind == QLatin1String("world") && !world_.isEmpty()) {
        sdk::DocumentGroup group;
        for (const CourseBank& bank : banks_) {
            if (bank.world.isEmpty()) continue;
            group.title = multipleBanks() ? bankLabel(bank.levels) : tr("World");
            int steps = 0;
            bool routesDirty = false;
            for (int w = 1; w <= document_.worldCount(bank.world); ++w) {
                const QVector<SmbRoute> now = document_.routes(bank.world, w);
                const QVector<SmbRoute> saved = saved_.routes(bank.world, w);
                steps += now.size();
                for (int s = 0; s < now.size() && s < saved.size(); ++s) {
                    routesDirty = routesDirty || now.at(s).areaType != saved.at(s).areaType ||
                                  now.at(s).areaIndex != saved.at(s).areaIndex ||
                                  now.at(s).alternate != saved.at(s).alternate;
                }
            }
            sdk::DocumentEntry routes =
                entry(bankDocument(bank.levels, QStringLiteral("routes")), tr("Routes"),
                      tr("%1 worlds · %2 steps").arg(document_.worldCount(bank.world)).arg(steps));
            routes.dirty = routesDirty;
            sdk::DocumentEntry physics = entry(bankDocument(bank.levels, QStringLiteral("physics")), tr("Player physics"),
                                               tr("%n tables", nullptr, document_.physicsTables(bank.world).size()));
            const QVector<SmbPhysicsTable> nowTables = document_.physicsTables(bank.world);
            const QVector<SmbPhysicsTable> savedTables = saved_.physicsTables(bank.world);
            for (int t = 0; t < nowTables.size() && t < savedTables.size(); ++t) {
                physics.dirty = physics.dirty || nowTables.at(t).values != savedTables.at(t).values;
            }
            group.documents << routes << physics;
            if (&bank != &banks_.constLast()) {
                groups << group;
                group = sdk::DocumentGroup();
            }
        }
        if (!art_.playerFrames.isEmpty()) {
            sdk::DocumentEntry actions = entry(QStringLiteral("actions"), tr("Player actions"),
                                               tr("frames per action"));
            const QJsonObject now = document_.resource(art_.playerFrames);
            const QJsonObject saved = saved_.resource(art_.playerFrames);
            actions.dirty = now.value(QStringLiteral("frame_offsets")) != saved.value(QStringLiteral("frame_offsets")) ||
                            now.value(QStringLiteral("swim_kick_tiles")) != saved.value(QStringLiteral("swim_kick_tiles"));
            group.documents << actions;
        }
        groups << group;
    } else if (kind == QLatin1String("animations") && !art_.playerFrames.isEmpty()) {
        QStringList names;
        for (const QJsonValue& v : document_.resource(art_.playerFrames).value(QStringLiteral("frame_names")).toArray()) {
            names << v.toString();
        }
        const QJsonArray now = document_.resource(art_.playerFrames).value(QStringLiteral("frames")).toArray();
        const QJsonArray saved = saved_.resource(art_.playerFrames).value(QStringLiteral("frames")).toArray();
        sdk::DocumentGroup big;
        big.title = tr("Big Hero");
        sdk::DocumentGroup small;
        small.title = tr("Small Hero");
        sdk::DocumentGroup other;
        other.title = tr("Other");
        for (const QString& animation : SmbPlayerFramesModel::animations(names)) {
            const QVector<int> frames = SmbPlayerFramesModel::framesOf(names, animation);
            QString label = SmbPlayerFramesModel::animationLabel(animation);
            sdk::DocumentGroup* group = &other;
            if (animation.startsWith(QLatin1String("big_"))) {
                group = &big;
                label = SmbPlayerFramesModel::animationLabel(animation.mid(4));
            } else if (animation.startsWith(QLatin1String("small_"))) {
                group = &small;
                label = SmbPlayerFramesModel::animationLabel(animation.mid(6));
            }
            sdk::DocumentEntry e = entry(animation, label,
                                         tr("%n frames", nullptr, frames.size()));
            for (int f : frames) {
                e.dirty = e.dirty || now.at(f) != saved.at(f);
            }
            group->documents << e;
        }
        for (const sdk::DocumentGroup& g : {big, small, other}) {
            if (!g.documents.isEmpty()) groups << g;
        }
    } else if (kind == QLatin1String("screens")) {
        for (const QString& id : document_.resourceIds(QStringLiteral("nes.ppu-screen-packets.v1"))) {
            const SmbScreenSet& set = screenSet(id);
            sdk::DocumentGroup group;
            group.title = id == QLatin1String("game-screen-packets") ? tr("Game screens")
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
    } else if (kind == QLatin1String("sound") && !music_.isEmpty()) {
        const SmbMusic music = SmbMusic::fromJson(document_.resource(music_));
        const SmbMusic saved = SmbMusic::fromJson(saved_.resource(music_));
        QStringList area;
        QStringList events;
        QStringList other;
        for (const auto& selector : music.selectors) {
            QStringList& list = selector.first.startsWith(QLatin1String("area_")) ? area : events;
            if (!list.contains(selector.second) && !area.contains(selector.second) && !events.contains(selector.second)) {
                list << selector.second;
            }
        }
        QStringList ground;
        for (const SmbMusicHeader& header : music.headers) {
            if (header.name.startsWith(QLatin1String("ground_")) && !area.contains(header.name)) {
                ground << header.name;
            } else if (!area.contains(header.name) && !events.contains(header.name)) {
                other << header.name;
            }
        }
        const auto group = [&](const QString& title, const QStringList& names) {
            sdk::DocumentGroup g;
            g.title = title;
            for (const QString& name : names) {
                QString meta;
                try {
                    meta = QStringLiteral("%1 s").arg(music.timeline(name).duration / 60.0988, 0, 'f', 1);
                } catch (const SmbError&) {
                }
                sdk::DocumentEntry e = entry(name, displayName(name), meta);
                // A song is edited when any byte its channels read changed.
                try {
                    for (const SmbMusicChannel& channel : music.timeline(name).channels) {
                        for (const SmbMusicEvent& ev : channel.events) {
                            e.dirty = e.dirty || music.arena.value(ev.offset) != saved.arena.value(ev.offset);
                        }
                    }
                } catch (const SmbError&) {
                }
                g.documents << e;
            }
            if (!g.documents.isEmpty()) groups << g;
        };
        group(tr("Area music"), area);
        group(tr("Ground theme parts"), ground);
        group(tr("Events"), events);
        group(tr("Other"), other);
        sdk::DocumentGroup effects;
        effects.title = tr("Sound effects");
        for (const nes::SoundEffect& effect : soundEffects()) {
            const QString queue = effect.key.section(QLatin1Char(':'), 1, 1);
            const QString channel = queue == QLatin1String("square1")   ? tr("Square 1")
                                    : queue == QLatin1String("square2") ? tr("Square 2")
                                                                        : tr("Noise");
            effects.documents << entry(QStringLiteral("sfx:") + effect.key, effect.title, channel);
        }
        const bool tables = document_.resourceIds(QStringLiteral("smb.sfx-tables.v1")).contains(kSfxTables);
        sdk::DocumentEntry e = entry(kSfxTables, tr("Sound effect tables"),
                                     tables ? QStringLiteral("81 B") : tr("not added yet"));
        e.dirty = tables && saved_.resourceIds(QStringLiteral("smb.sfx-tables.v1")).contains(kSfxTables) &&
                  document_.resource(kSfxTables) != saved_.resource(kSfxTables);
        effects.documents << e;
        groups << effects;
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        sdk::DocumentEntry e = entry(QStringLiteral("all"), tr("Game text"),
                                     tr("%n strings", nullptr, textSlots().size()));
        for (int i = 0; i < textSlots().size(); ++i) {
            e.dirty = e.dirty || text(i) != slotText(saved_, textSlots().at(i));
        }
        group.documents << e;
        groups << group;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup group;
        group.title = tr("Title");
        if (demo_.isEmpty()) {
            // Opening it adds the document from the project's own source.
            group.documents << entry(QStringLiteral("demo"), tr("Title demo"), tr("not added yet"));
        } else {
            sdk::DocumentEntry e = entry(QStringLiteral("demo"), tr("Title demo"),
                                         tr("%n steps", nullptr, demo_.value(QStringLiteral("steps")).toArray().size()));
            e.dirty = demo_ != savedDemo_;
            group.documents << e;
        }
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

std::unique_ptr<sdk::EditorModel> SmbSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("levels")) {
            const QString bank = document.contains(QLatin1Char('/')) ? document.section(QLatin1Char('/'), 0, 0) : levels_;
            const QString area = document.section(QLatin1Char('/'), -1);
            if (document_.resourceIds(QStringLiteral("smb.levels.v1")).contains(bank) &&
                document_.areaNames(bank).contains(area)) {
                useBank(bank);
                selectStart(area);
                return std::make_unique<SmbLevelModel>(this, area);
            }
        }
        if (kind == QLatin1String("graphics") &&
            (document == QLatin1String("sprites") || document == QLatin1String("background"))) {
            return std::make_unique<SmbTileModel>(this, document == QLatin1String("background"));
        }
        if (kind == QLatin1String("animations") && !art_.playerFrames.isEmpty() && !art_.playerPalette.isEmpty()) {
            QStringList names;
            for (const QJsonValue& v : document_.resource(art_.playerFrames).value(QStringLiteral("frame_names")).toArray()) {
                names << v.toString();
            }
            if (SmbPlayerFramesModel::animations(names).contains(document)) {
                return std::make_unique<SmbPlayerFramesModel>(this, document);
            }
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
            if (document.contains(QLatin1Char('/'))) useBank(document.section(QLatin1Char('/'), 0, 0));
            const QString view = document.section(QLatin1Char('/'), -1);
            if (view == QLatin1String("routes")) {
                return std::make_unique<SmbRoutesModel>(this);
            }
            if (view == QLatin1String("physics")) {
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
        if (kind == QLatin1String("sound") && (document.startsWith(QLatin1String("sfx:")) || document == kSfxTables)) {
            // The effect tables came with a newer kit.
            if (!document_.resourceIds(QStringLiteral("smb.sfx-tables.v1")).contains(kSfxTables)) {
                upgradeProject(kind, document);
                return nullptr;
            }
            if (document == kSfxTables) return std::make_unique<SmbSfxTablesModel>(this);
            for (const nes::SoundEffect& effect : soundEffects()) {
                if (QStringLiteral("sfx:") + effect.key == document) {
                    return std::make_unique<nes::SoundEffectModel>(this, effect, project_.path(), project_.directory(),
                                                                   soundEffectData(this));
                }
            }
        }
        if (kind == QLatin1String("sound") && !music_.isEmpty() &&
            SmbMusic::fromJson(document_.resource(music_)).indexOf(document) >= 0) {
            return std::make_unique<SmbMusicModel>(this, document);
        }
        if (kind == QLatin1String("text") && document == QLatin1String("all")) {
            return std::make_unique<SmbTextModel>(this);
        }
        if (kind == QLatin1String("demo")) {
            if (document == QLatin1String("demo") && demo_.isEmpty()) {
                upgradeProject(kind, document);
                return nullptr;
            }
            if (document == QLatin1String("demo") && !demo_.isEmpty()) return std::make_unique<SmbDemoModel>(this);
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

bool SmbSession::save(QString* error) {
    try {
        if (revision_ != savedRevision_) {
            document_.save(documentPath_);
        }
        if (demo_ != savedDemo_ && !demoPath_.isEmpty()) saveJsonObject(demoPath_, demo_);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    saved_ = document_;
    savedRevision_ = revision_;
    savedDemo_ = demo_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return true;
}

QVector<sdk::Diagnostic> SmbSession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    const SmbLevelPool pool = document_.levelPool(levels_);
    if (pool.pooled) {
        const auto check = [&](const QString& stream, int used, int size) {
            if (used <= size) return;
            sdk::Diagnostic d;
            d.severity = sdk::Severity::Error;
            d.source = QStringLiteral("levels");
            d.kind = QStringLiteral("levels");
            d.location = tr("All levels");
            d.message = tr("The %1 streams of all levels need %2 of %3 bytes.").arg(stream).arg(used).arg(size);
            result << d;
        };
        check(QStringLiteral("object"), pool.objectUsed, pool.objectSize);
        check(QStringLiteral("enemy"), pool.enemyUsed, pool.enemySize);
        return result;
    }
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
            d.message = tr("The %1 stream uses %2 of %3 bytes.").arg(stream).arg(used).arg(capacity);
            result << d;
        };
        check(QStringLiteral("object"), area.areaFits(), area.areaBytes(), area.areaCapacity);
        check(QStringLiteral("enemy"), area.enemiesFit(), area.enemyBytes(), area.enemyCapacity);
    }
    return result;
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

void SmbSession::useBank(const QString& levels) {
    for (const CourseBank& bank : banks_) {
        if (bank.levels == levels) {
            levels_ = bank.levels;
            world_ = bank.world;
        }
    }
}

QString SmbSession::bankLabel(const QString& levels) const {
    const QString kind = levels.section(QLatin1Char('-'), -1);
    if (kind == QLatin1String("normal")) return tr("Main courses");
    if (kind == QLatin1String("extended")) return tr("Extra courses");
    if (kind == QLatin1String("hard")) return tr("Hard courses");
    return displayName(levels);
}

QString SmbSession::bankDocument(const QString& levels, const QString& document) const {
    return multipleBanks() ? levels + QLatin1Char('/') + document : document;
}

QPair<int, int> SmbSession::courseOf(const QString& area) const {
    return courseOf(area, levels_, world_);
}

QPair<int, int> SmbSession::courseOf(const QString& area, const QString& levels, const QString& world) const {
    if (world.isEmpty()) {
        return {0, 0};
    }
    const int type = document_.areaTypes(levels).indexOf(areaTypeOf(area));
    const int index = area.section(QLatin1Char('_'), -1).toInt() - 1;
    for (int w = 1; w <= document_.worldCount(world); ++w) {
        const QVector<SmbRoute> routes = document_.routes(world, w);
        for (int c = 0; c < routes.size(); ++c) {
            if (routes.at(c).areaType == type && routes.at(c).areaIndex == index) {
                return {w, c + 1};
            }
        }
    }
    return {0, 0};
}

QPair<int, int> SmbSession::levelOf(const QString& area) const {
    return levelOf(area, levels_, world_);
}

QPair<int, int> SmbSession::levelOf(const QString& area, const QString& levels, const QString& world) const {
    const QPair<int, int> course = courseOf(area, levels, world);
    if (course.first <= 0) {
        return course;
    }
    const QStringList types = document_.areaTypes(levels);
    const QVector<SmbRoute> routes = document_.routes(world, course.first);
    int level = 1;
    for (int c = 0; c + 1 < course.second && c < routes.size(); ++c) {
        const QString name = QStringLiteral("%1_%2").arg(types.value(routes.at(c).areaType)).arg(routes.at(c).areaIndex + 1);
        const int entrance = document_.area(levels, name).header.value(1);
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
    if (demoStart()) return tr("Title demo");
    if (start_.area.isEmpty()) {
        return {};
    }
    return tr("%1 · column %2").arg(areaLabel(start_.area)).arg(start_.cell.x());
}

QImage SmbSession::areaPreview(const QString& area, int columns, int firstColumn) const {
    const SmbArea a = document_.area(levels_, area);
    const SmbLevelScene scene =
        renderSmbLevelScene(area, a.header, a.objects, a.enemies, std::max(0, courseOf(area).first - 1),
                            SmbLevelRendering::fromId(document_.rendering(levels_)));
    const QImage image = painter_->render(scene, paletteKey(area), night(area));
    const int left = std::clamp(firstColumn * SmbLevelPainter::kCell, 0, std::max(0, image.width() - 1));
    return image.copy(left, 0, std::min(image.width() - left, columns * SmbLevelPainter::kCell), image.height());
}

QString SmbSession::paletteKey(const QString& area) const {
    // As the game loads them: GetAreaPalette by type, then GetBGPlayerColor
    // (background colour control 5 day snow, 6 night snow, 7 the castle
    // palette), GetAlternatePalette1 (mushroom style) and the axe object
    // (Boss's sprite palette).
    const SmbArea a = document_.area(levels_, area);
    const QStringList packets = document_.packetNames(art_.palettes);
    QString key = areaTypeOf(area);
    const auto add = [&](const char* name) {
        if (packets.contains(QLatin1String(name))) key += QLatin1Char('+') + QLatin1String(name);
    };
    switch (a.header.value(2)) {
    case 5: add("day_snow"); break;
    case 6: add("night_snow"); break;
    case 7: add("castle"); break;
    default: break;
    }
    if (a.header.value(3) == 1) {
        add("mushroom");
    }
    for (const SmbAreaObject& object : a.objects) {
        if (object.row == 13 && object.control == 0x42) {
            add("boss");
            break;
        }
    }
    return key;
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

void SmbSession::build() {
    runBuild([](bool) {}, true);
}

void SmbSession::play() {
    launch(true);
}

void SmbSession::playSelected() {
    if (isRecording()) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launch(false);
}

QString SmbSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

// ---------------------------------------------------------------- demo

int SmbSession::demoCapacity() const {
    const int capacity = demo_.value(QStringLiteral("layout")).toObject().value(QStringLiteral("capacity")).toInt();
    return capacity > 0 ? capacity : 21;
}

void SmbSession::setDemo(const QJsonObject& demo) {
    demo_ = demo;
    emit contentChanged();
    emit documentsChanged(QStringLiteral("demo"));
}

bool SmbSession::editDemo(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error) {
    QJsonObject next = demo_;
    change(next);
    if (next == demo_) return true;
    const QJsonArray steps = next.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty() || steps.size() > demoCapacity()) {
        if (error) *error = tr("The demo holds 1-%1 steps.").arg(demoCapacity());
        return false;
    }
    undoStack()->push(new sdk::SnapshotCommand<QJsonObject>(
        [this](const QJsonObject& value, quint64) { setDemo(value); }, demo_, 0, next, 0, text));
    return true;
}

void SmbSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) {
        emit startChanged();
        emit selectedStartChanged();
    }
}

void SmbSession::playDemo(bool record) {
    if (!isRecording()) launchDemo(record);
}

void SmbSession::upgradeProject(const QString& kind, const QString& document) {
    UpgradeStep step;
    step.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("smb-upgrade"), project_.path()};
    step.label = tr("Add what this kit edits to the project");
    step.source = kind;
    step.failure = tr("Upgrading the project failed: %1");
    step.saveFirst = true;
    step.adopt = [this] {
        const QString demo = tabDocument(project_, QStringLiteral("smb.demo@1"));
        if (!demo.isEmpty() && demo_.isEmpty()) {
            demoPath_ = project_.resolve(demo);
            demo_ = loadJsonObject(demoPath_);
            savedDemo_ = demo_;
        }
        // Resources the bundle gained; it was saved before the upgrade,
        // and older undo steps would drop them again.
        const SmbResourceDocument bundle = SmbResourceDocument::load(documentPath_);
        if (bundle.root() != document_.root()) {
            undoStack()->clear();
            saved_ = bundle;
            savedRevision_ = nextRevision_;
            // Through setDocument: the painter's cache and the open
            // editors follow the new resources.
            setDocument(bundle, nextRevision_++);
        }
    };
    step.done = tr("Added the documents and resources this kit edits to the project.");
    step.openKind = kind;
    step.openDocument = document;
    runUpgrade(step);
}

void SmbSession::launchDemo(bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("smb-demo-plan"), project_.path()};
    plan.label = record ? tr("Plan demo recording") : tr("Plan title demo");
    plan.title = record ? tr("Super Mario Bros. · recording the demo") : tr("Super Mario Bros. · title demo");
    plan.demo = true;
    plan.force = false;
    // The Playtest view covers the way to the demo.
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("SMB_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage = tr("Recording: play 1-1 with the controller; it ends when Hero dies, on Start or "
                                   "on Stop recording.");
        plan.recorded = [this](const QString& path) { applyRecording(path); };
    }
    plan.prepare = [this](sdk::ProcessSpec& game) { useColours(game); };
    launchPlanned(plan);
}

void SmbSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const int capacity = demoCapacity();
    const SmbDemoModel::Recording recording = SmbDemoModel::stepsFromRecording(QString::fromUtf8(file.readAll()), capacity);
    if (!recording.pressed) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               tr("No buttons were pressed while recording; the demo stays as it was."));
        return;
    }
    const QJsonArray steps = recording.steps;
    QString error;
    if (!editDemo(tr("Record the title demo"),
                  [steps](QJsonObject& demo) { demo.insert(QStringLiteral("steps"), steps); }, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    QString message = tr("Recorded %1 s of input into the demo's %2 steps.")
                          .arg(recording.keptFrames / 60.0, 0, 'f', 1)
                          .arg(steps.size());
    if (recording.droppedFrames > 0) {
        message += tr(" The game's tables hold %1 steps; the last %2 s did not fit.")
                       .arg(capacity)
                       .arg(recording.droppedFrames / 60.0, 0, 'f', 1);
    }
    report(recording.droppedFrames > 0 ? sdk::Severity::Warning : sdk::Severity::Info, QStringLiteral("demo"), message);
}

void SmbSession::launch(bool fromStart) {
    const SmbStart start = start_;
    EmulatorPlan plan;
    plan.force = false;
    if (fromStart) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("Super Mario Bros. · power-on");
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("smb-point-playtest-plan"), project_.path(),
                          QStringLiteral("--course-bank"), levels_, QStringLiteral("--area"), start.area,
                          QStringLiteral("--world"), QString::number(start.world), QStringLiteral("--course"),
                          QString::number(start.course), QStringLiteral("--column"), QString::number(start.cell.x()),
                          QStringLiteral("--row"), QString::number(start.cell.y()), QStringLiteral("--theme"),
                          night(start.area) ? QStringLiteral("night") : QStringLiteral("day"),
                          QStringLiteral("--sfx-channel"), QStringLiteral("none"), QStringLiteral("--sfx-value"),
                          QStringLiteral("0")};
        plan.label = tr("Plan playtest of %1").arg(areaLabel(start.area));
        plan.title = tr("Super Mario Bros. · %1").arg(areaLabel(start.area));
    }
    plan.prepare = [this, fromStart](sdk::ProcessSpec& game) {
        useColours(game);
        // The kit's point script reports nothing; the game is up once it
        // plays the area with Hero placed on the start page (operating
        // mode 1, task 3, player engine running), so the boot and the
        // steering can run hidden and fast.
        const QString page = game.environment.value(QStringLiteral("SMB1_PLAYTEST_PAGE"));
        if (!fromStart && !page.isEmpty()) {
            game.readyWhen = QStringLiteral("memory:read_u8(0x0770) == 1 and memory:read_u8(0x0772) == 3 and "
                                            "memory:read_u8(0x000e) >= 7 and memory:read_u8(0x006d) == %1 and "
                                            "memory:read_u8(0x071a) == %1")
                                 .arg(page.toInt());
        }
    };
    launchPlanned(plan);
}

void SmbSession::useColours(sdk::ProcessSpec& game) const {
    // The game in the colours the editors draw with.
    const std::string model = document_.paletteModel(art_.palettes).toStdString();
    for (int i = 0; i < 64; ++i) {
        game.palette << nes::masterColor(i, model);
    }
}

}  // namespace rl::smb
