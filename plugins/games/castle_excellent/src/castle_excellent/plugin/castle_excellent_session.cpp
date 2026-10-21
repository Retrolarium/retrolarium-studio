#include "castle_excellent/plugin/castle_excellent_session.hpp"

#include "castle_excellent/plugin/animation_model.hpp"
#include "castle_excellent/plugin/cell_model.hpp"
#include "castle_excellent/plugin/demo_model.hpp"
#include "castle_excellent/plugin/map_model.hpp"
#include "castle_excellent/plugin/music_models.hpp"
#include "castle_excellent/plugin/room_model.hpp"
#include "castle_excellent/plugin/screen_model.hpp"
#include "nes/sound_effect_model.hpp"
#include "castle_excellent/plugin/sprite_model.hpp"
#include "castle_excellent/plugin/table_models.hpp"
#include "nes/palette.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QUndoStack>

#include <algorithm>

namespace rl::castle_excellent {

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

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta = QString(), bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

QString first(const nes::Bundle& bundle, const char* codec) {
    const QStringList ids = bundle.resourceIds(QString::fromLatin1(codec));
    return ids.isEmpty() ? QString() : ids.first();
}

// The five start routines of the sound effect engine ($C20D-$C2BD); each
// is played by UpdateSoundEffects ($C2F9) from the next frame.
QVector<nes::SoundEffect> soundEffects() {
    const QString tuned = rlTr("castle_excellent", "Its tunable bytes are in SFX parameters.");
    return {{QStringLiteral("castle-excellent:1"), rlTr("castle_excellent", "Jump"), rlTr("castle_excellent", "Start routine $C20D"), QString()},
            {QStringLiteral("castle-excellent:2"), rlTr("castle_excellent", "Crush"),
             rlTr("castle_excellent", "Start routine $C233"), QString()},
            {QStringLiteral("castle-excellent:3"), rlTr("castle_excellent", "Item collected"),
             rlTr("castle_excellent", "Start routine $C272"), tuned},
            {QStringLiteral("castle-excellent:4"), rlTr("castle_excellent", "Flame"), rlTr("castle_excellent", "Start routine $C29A"), tuned},
            {QStringLiteral("castle-excellent:5"), rlTr("castle_excellent", "Cell opens"),
             rlTr("castle_excellent", "Start routine $C2BD"), QString()}};
}

}  // namespace

QString roomLabel(int room) {
    return rlTr("castle_excellent", "Room %1").arg(room);
}

// ---------------------------------------------------------------- opening

std::unique_ptr<CastleExcellentSession> CastleExcellentSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<CastleExcellentSession> session(new CastleExcellentSession);
    session->project_ = project;
    try {
        const QString levels = tabDocument(project, QStringLiteral("castle-excellent.prefix-rooms@1"));
        const QString graphics = tabDocument(project, QStringLiteral("nes.chr-2bpp.v1"));
        if (levels.isEmpty() || graphics.isEmpty()) {
            throw CastleExcellentError("The project has no Castle rooms or graphics tab.");
        }
        session->levelsPath_ = project.resolve(levels);
        session->graphicsPath_ = project.resolve(graphics);
        session->state_.levels = CastleExcellentLevels::load(session->levelsPath_);
        session->state_.graphics = nes::Bundle::load(session->graphicsPath_);
        const QString demo = tabDocument(project, QStringLiteral("castle-excellent.demo@1"));
        if (!demo.isEmpty()) {
            session->demoPath_ = project.resolve(demo);
            session->state_.demo = loadJsonObject(session->demoPath_);
        }
        const QString screensDocument = tabDocument(project, QStringLiteral("castle-excellent.screens@1"));
        if (!screensDocument.isEmpty()) {
            session->screensPath_ = project.resolve(screensDocument);
            session->state_.screens = loadJsonObject(session->screensPath_);
        }
        session->saved_ = session->state_;
        const nes::Bundle& g = session->state_.graphics;
        session->art_.chr = QStringLiteral("chr-bank-%1").arg(session->state_.levels.chrBank());
        if (!g.contains(session->art_.chr)) {
            session->art_.chr = first(g, "nes.chr-2bpp.v1");
        }
        session->art_.roomPalette = g.contains(QStringLiteral("room-palettes")) ? QStringLiteral("room-palettes")
                                                                                : first(g, "nes.palette-table.v1");
        session->art_.cellVisuals = first(g, "castle-excellent.room-cell-visuals.v1");
        session->art_.descriptors = first(g, "castle-excellent.metasprite-descriptors.v1");
        session->musicId_ = first(g, "castle-excellent.mml-music.v1");
        if (session->art_.chr.isEmpty() || session->art_.roomPalette.isEmpty() ||
            g.tableValues(session->art_.roomPalette).size() < 32) {
            throw CastleExcellentError("The graphics bundle lacks the room patterns or palette.");
        }
        session->painter();
    } catch (const std::exception& e) {
        if (error) {
            *error = tr("Cannot open the Castle project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile CastleExcellentSession::profile() const {
    sdk::Profile profile;
    const bool usa = state_.levels.revision().contains(QLatin1String("usa"));
    profile.name = usa ? tr("Castlequest") : tr("Castle Excellent");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    profile.revision = usa ? QStringLiteral("U") : QStringLiteral("J");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("castle-excellent.prefix-rooms@1");
    return profile;
}

QVector<sdk::Kind> CastleExcellentSession::kinds() const {
    return {kind("rooms", "Levels", "levels", sdk::EditorType::TileMap),
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

QVector<sdk::DocumentGroup> CastleExcellentSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("rooms")) {
        sdk::DocumentGroup overview;
        overview.title = tr("Overview");
        overview.documents << entry(QStringLiteral("map"), tr("Castle map"), QStringLiteral("10 × 10"));
        groups << overview;
        sdk::DocumentGroup group;
        group.title = tr("Castle");
        for (int r = 0; r < state_.levels.roomCount(); ++r) {
            const QVector<CastleExcellentObject> objects = state_.levels.objects(r);
            const bool dirty = state_.levels.cells(r) != saved_.levels.cells(r) || objects != saved_.levels.objects(r);
            group.documents << entry(QString::number(r + 1), roomLabel(state_.levels.roomNumber(r)),
                                     objects.isEmpty() ? QString() : tr("%1 obj").arg(objects.size()),
                                     dirty);
        }
        groups << group;
    } else if (kind == QLatin1String("graphics")) {
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1"))) {
            sdk::DocumentGroup group;
            const int count = state_.graphics.tileCount(id);
            group.title = id == art_.chr ? tr("%1 · rooms").arg(id) : id;
            for (int half = 0; half * 256 < count; ++half) {
                bool dirty = false;
                for (int t = half * 256; t < std::min(count, half * 256 + 256) && !dirty; ++t) {
                    dirty = state_.graphics.tilePixels(id, t) != saved_.graphics.tilePixels(id, t);
                }
                group.documents << entry(QStringLiteral("%1/%2").arg(id).arg(half),
                                         half == 0 ? tr("Pattern table 0") : tr("Pattern table 1"),
                                         id == art_.chr ? (half == 0 ? QStringLiteral("sprites")
                                                                     : tr("room cells"))
                                                        : tr("256 tiles"),
                                         dirty);
            }
            groups << group;
        }
    } else if (kind == QLatin1String("metatiles")) {
        sdk::DocumentGroup group;
        group.title = tr("Metatiles");
        if (!art_.cellVisuals.isEmpty()) {
            group.documents << entry(QStringLiteral("cells"), tr("Room metatiles"),
                                     QStringLiteral("%1 · 16x8").arg(painter().cellTypeCount()),
                                     state_.graphics.resource(art_.cellVisuals) != saved_.graphics.resource(art_.cellVisuals));
        }
        groups << group;
    } else if (kind == QLatin1String("animations") && !art_.descriptors.isEmpty()) {
        const QJsonArray now = state_.graphics.resource(art_.descriptors).value(QStringLiteral("descriptors")).toArray();
        const QJsonArray saved = saved_.graphics.resource(art_.descriptors).value(QStringLiteral("descriptors")).toArray();
        QVector<sdk::DocumentGroup> byGroup;
        const QVector<CastleExcellentAnimation>& animations = castleExcellentAnimations();
        for (int i = 0; i < animations.size(); ++i) {
            const CastleExcellentAnimation& a = animations.at(i);
            bool dirty = false;
            for (const CastleExcellentAnimationFrame& f : a.frames) {
                for (int s = 0; s < f.offsets.size(); ++s) dirty = dirty || now.at(f.first + s) != saved.at(f.first + s);
            }
            auto group = std::find_if(byGroup.begin(), byGroup.end(),
                                      [&a](const sdk::DocumentGroup& g) { return g.title == a.group; });
            if (group == byGroup.end()) {
                sdk::DocumentGroup g;
                g.title = a.group;
                byGroup << g;
                group = byGroup.end() - 1;
            }
            group->documents << entry(QStringLiteral("anim:%1").arg(i), a.label,
                                      a.frames.size() > 1 ? tr("%1 fr").arg(a.frames.size()) : QString(), dirty);
        }
        sdk::DocumentGroup sheet;
        sheet.title = tr("Sprites");
        sheet.documents << entry(QStringLiteral("sprites"), tr("All sprites"), QStringLiteral("8x16"), now != saved);
        groups << byGroup << sheet;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup group;
        group.title = tr("Palettes");
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1"))) {
            if (id == QLatin1String("screen-palettes")) continue;
            const bool dirty = state_.graphics.tableValues(id) != saved_.graphics.tableValues(id);
            group.documents << entry(id, id == QLatin1String("room-palettes") ? tr("Rooms") : id,
                                     tr("%n colours", nullptr, state_.graphics.tableValues(id).size()), dirty);
        }
        groups << group;
        if (state_.graphics.contains(QStringLiteral("screen-palettes"))) {
            // One 64-byte store: each screen loads 32 colours from its own
            // place in it, so neighbouring screens share 16.
            sdk::DocumentGroup screenGroup;
            screenGroup.title = tr("Screens");
            const QVector<int> now = state_.graphics.tableValues(QStringLiteral("screen-palettes"));
            const QVector<int> saved = saved_.graphics.tableValues(QStringLiteral("screen-palettes"));
            for (const QString& id : screens::ids()) {
                const int first = screens::paletteFirst(id);
                screenGroup.documents << entry(QStringLiteral("screen:") + id, screens::label(id),
                                               tr("32 colours"), now.mid(first, 32) != saved.mid(first, 32));
            }
            groups << screenGroup;
        }
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        group.documents << entry(QStringLiteral("ending"), tr("Ending & messages"),
                                 state_.screens.isEmpty() ? tr("not added yet") : QString(),
                                 !state_.screens.isEmpty() && state_.screens != saved_.screens);
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle-excellent.sub-command-text.v1"))) {
            group.documents << entry(id, tr("Data recorder menu"), QString(),
                                     state_.graphics.resource(id) != saved_.graphics.resource(id));
        }
        groups << group;
    } else if (kind == QLatin1String("screens")) {
        sdk::DocumentGroup group;
        group.title = tr("Screens");
        const QJsonArray list = state_.screens.value(QStringLiteral("screens")).toArray();
        const QJsonArray saved = saved_.screens.value(QStringLiteral("screens")).toArray();
        for (int i = 0; i < screens::ids().size(); ++i) {
            const QString id = screens::ids().at(i);
            group.documents << entry(id, screens::label(id), list.isEmpty() ? tr("not added yet")
                                                                            : QStringLiteral("32 × 30"),
                                     !list.isEmpty() && list.at(i) != saved.at(i));
        }
        groups << group;
    } else if (kind == QLatin1String("sound") && !musicId_.isEmpty()) {
        const QJsonObject now = music();
        const QJsonObject saved = savedMusic();
        sdk::DocumentGroup tracks;
        tracks.title = tr("Track sets");
        const QJsonArray sets = now.value(QStringLiteral("track_sets")).toArray();
        for (int t = 0; t < sets.size(); ++t) {
            const QJsonObject set = sets.at(t).toObject();
            // A track set is edited with the streams it plays.
            bool dirty = set != saved.value(QStringLiteral("track_sets")).toArray().at(t).toObject();
            QSet<QString> ids;
            for (const QJsonValue& id : set.value(QStringLiteral("streams")).toArray()) ids.insert(id.toString());
            const QJsonArray streams = now.value(QStringLiteral("streams")).toArray();
            for (int i = 0; i < streams.size() && !dirty; ++i) {
                dirty = ids.contains(streams.at(i).toObject().value(QStringLiteral("id")).toString()) &&
                        streams.at(i) != saved.value(QStringLiteral("streams")).toArray().at(i);
            }
            tracks.documents << entry(QStringLiteral("track:%1").arg(t), set.value(QStringLiteral("name")).toString(),
                                      QString::number(t), dirty);
        }
        sdk::DocumentGroup text;
        text.title = tr("Sequences");
        const QJsonArray sequences = now.value(QStringLiteral("streams")).toArray();
        for (int i = 0; i < sequences.size(); ++i) {
            const QJsonObject s = sequences.at(i).toObject();
            text.documents << entry(QStringLiteral("stream:%1").arg(i),
                                    tr("Sequence %1").arg(i, 2, 10, QLatin1Char('0')),
                                    QStringLiteral("%1 B").arg(s.value(QStringLiteral("text")).toString().size() + 3),
                                    sequences.at(i) != saved.value(QStringLiteral("streams")).toArray().at(i));
        }
        text.documents << entry(QStringLiteral("streams"), tr("All sequences as text"),
                                QStringLiteral("%1 B").arg(CastleExcellentMusic::usedBytes(now)),
                                now.value(QStringLiteral("streams")) != saved.value(QStringLiteral("streams")));
        sdk::DocumentGroup effects;
        effects.title = tr("Sound effects");
        for (const nes::SoundEffect& effect : soundEffects()) {
            effects.documents << entry(QStringLiteral("sfx:") + effect.key, effect.title, QString());
        }
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle-excellent.sfx-parameters.v1"))) {
            effects.documents << entry(id, tr("SFX parameters"), QString(),
                                       state_.graphics.resource(id) != saved_.graphics.resource(id));
        }
        groups << tracks << text << effects;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup group;
        group.title = tr("Title");
        if (state_.demo.isEmpty()) {
            // Opening it adds the document from the project's own source.
            group.documents << entry(QStringLiteral("demo"), tr("Title demo"), tr("not added yet"));
        } else {
            group.documents << entry(QStringLiteral("demo"), tr("Title demo"),
                                     tr("%n steps", nullptr, state_.demo.value(QStringLiteral("steps")).toArray().size()),
                                     state_.demo != saved_.demo);
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

std::unique_ptr<sdk::EditorModel> CastleExcellentSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("rooms") && document == QLatin1String("map")) {
            return std::make_unique<CastleExcellentMapModel>(this);
        }
        if (kind == QLatin1String("rooms")) {
            const int room = document.toInt() - 1;
            if (room >= 0 && room < state_.levels.roomCount()) {
                return std::make_unique<CastleExcellentRoomModel>(this, room);
            }
        }
        if (kind == QLatin1String("graphics")) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1")).contains(id) &&
                half * 256 < state_.graphics.tileCount(id)) {
                // Rooms draw sprites from table 0 and cells from table 1.
                return std::make_unique<nes::ChrTileModel>(
                    this, id, half * 256, std::min(256, state_.graphics.tileCount(id) - half * 256),
                    tr("%1 · table %2").arg(id).arg(half), half == 0);
            }
        }
        if (kind == QLatin1String("palettes") && document.startsWith(QLatin1String("screen:")) &&
            state_.graphics.contains(QStringLiteral("screen-palettes"))) {
            const QString id = document.mid(7);
            auto model = std::make_unique<nes::PaletteTableModel>(this, QStringLiteral("screen-palettes"),
                                                                  tr("%1 palette").arg(screens::label(id)));
            model->setWindow(screens::paletteFirst(id), 32, document);
            return model;
        }
        if (kind == QLatin1String("palettes") &&
            state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1")).contains(document)) {
            return std::make_unique<nes::PaletteTableModel>(this, document, tr("Room palettes"));
        }
        if ((kind == QLatin1String("screens") || (kind == QLatin1String("text") && document == QLatin1String("ending"))) &&
            state_.screens.isEmpty()) {
            upgradeDocuments(kind, document);
            return nullptr;
        }
        if (kind == QLatin1String("screens") && screens::ids().contains(document)) {
            return std::make_unique<CastleExcellentScreenModel>(this, screens::ids().indexOf(document));
        }
        if (kind == QLatin1String("text") && document == QLatin1String("ending")) {
            return std::make_unique<CastleExcellentScreenTextModel>(this);
        }
        if (kind == QLatin1String("text") &&
            state_.graphics.resourceIds(QStringLiteral("castle-excellent.sub-command-text.v1")).contains(document)) {
            return std::make_unique<CastleExcellentTextModel>(this, document);
        }
        if (kind == QLatin1String("metatiles") && document == QLatin1String("cells") && !art_.cellVisuals.isEmpty()) {
            return std::make_unique<CastleExcellentCellModel>(this);
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:")) && !art_.descriptors.isEmpty()) {
            const int index = document.mid(5).toInt();
            if (index >= 0 && index < castleExcellentAnimations().size()) return std::make_unique<CastleExcellentAnimationModel>(this, index);
        }
        if (kind == QLatin1String("animations") && document == QLatin1String("sprites") && !art_.descriptors.isEmpty()) {
            return std::make_unique<CastleExcellentSpriteModel>(this);
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("sfx:"))) {
            const QStringList watched = state_.graphics.resourceIds(QStringLiteral("castle-excellent.sfx-parameters.v1"));
            for (const nes::SoundEffect& effect : soundEffects()) {
                if (QStringLiteral("sfx:") + effect.key == document) {
                    return std::make_unique<nes::SoundEffectModel>(this, effect, project_.path(), project_.directory(),
                                                                   nes::bundleSoundEffectData(this, watched));
                }
            }
        }
        if (kind == QLatin1String("sound") &&
            state_.graphics.resourceIds(QStringLiteral("castle-excellent.sfx-parameters.v1")).contains(document)) {
            return std::make_unique<CastleExcellentSfxModel>(this, document);
        }
        if (kind == QLatin1String("sound") && !musicId_.isEmpty()) {
            if (document == QLatin1String("streams")) return std::make_unique<CastleExcellentStreamsModel>(this);
            if (document.startsWith(QLatin1String("stream:"))) {
                const int stream = document.mid(7).toInt();
                if (stream >= 0 && stream < music().value(QStringLiteral("streams")).toArray().size()) {
                    return std::make_unique<CastleExcellentSequenceModel>(this, stream);
                }
            }
            const int track = document.startsWith(QLatin1String("track:")) ? document.mid(6).toInt() : -1;
            if (track >= 0 && track < music().value(QStringLiteral("track_sets")).toArray().size()) {
                return std::make_unique<CastleExcellentMusicModel>(this, track);
            }
        }
        if (kind == QLatin1String("demo")) {
            if (document == QLatin1String("demo") && state_.demo.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            if (document == QLatin1String("demo") && !state_.demo.isEmpty()) {
                return std::make_unique<CastleExcellentDemoModel>(this);
            }
        }
        if (kind == QLatin1String("playtest")) {
            return std::make_unique<CastleExcellentPlaytestModel>(this);
        }
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const CastleExcellentPainter& CastleExcellentSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<CastleExcellentPainter>(state_.levels, state_.graphics, art_);
        painterRevision_ = revision_;
    }
    return *painter_;
}

void CastleExcellentSession::setState(const CastleExcellentState& state, quint64 revision) {
    const CastleExcellentState current = state_;
    state_ = state;
    // Snapshots from before the kit added a document keep it.
    if (state_.demo.isEmpty()) state_.demo = current.demo;
    if (state_.screens.isEmpty()) state_.screens = current.screens;
    revision_ = revision;
    emit contentChanged();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
}

bool CastleExcellentSession::edit(const QString& text, const std::function<void(CastleExcellentState&)>& change, QString* error) {
    CastleExcellentState next = state_;
    try {
        change(next);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    if (next == state_) {
        return true;
    }
    undoStack()->push(new sdk::SnapshotCommand<CastleExcellentState>(
        [this](const CastleExcellentState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, text));
    return true;
}

bool CastleExcellentSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change,
                               QString* error) {
    return edit(text, [change](CastleExcellentState& s) { change(s.graphics); }, error);
}

bool CastleExcellentSession::save(QString* error) {
    // Each document counts as saved once written: a save that fails
    // halfway leaves the ones before it saved.
    try {
        if (state_.levels != saved_.levels) {
            state_.levels.save(levelsPath_);
            saved_.levels = state_.levels;
        }
        if (state_.graphics != saved_.graphics) {
            state_.graphics.save(graphicsPath_);
            saved_.graphics = state_.graphics;
        }
        if (state_.demo != saved_.demo && !demoPath_.isEmpty()) {
            saveJsonObject(demoPath_, state_.demo);
            saved_.demo = state_.demo;
        }
        if (state_.screens != saved_.screens && !screensPath_.isEmpty()) {
            saveJsonObject(screensPath_, state_.screens);
            saved_.screens = state_.screens;
        }
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    saved_ = state_;
    undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return true;
}

CastleExcellentStart CastleExcellentSession::start() const {
    CastleExcellentStart start;
    start.room = start_.room;
    const auto placed = placedStarts_.constFind(start.room);
    if (placed != placedStarts_.constEnd()) {
        start.pixel = *placed;
        start.placed = true;
    }
    return start;
}

void CastleExcellentSession::setStart(const CastleExcellentStart& start) {
    const int room = std::clamp(start.room, 1, std::max(1, state_.levels.roomCount()));
    const CastleExcellentStart current = this->start();
    if (start.placed) {
        // The player's top-left; the game keeps its feet, 16 pixels lower.
        const QPoint pixel(std::clamp(start.pixel.x(), 0, 304), std::clamp(start.pixel.y(), 0, 143));
        if (room == current.room && current.placed && pixel == current.pixel) {
            return;
        }
        placedStarts_.insert(room, pixel);
    } else if (room == current.room) {
        return;
    }
    start_.room = room;
    emit startChanged();
    emit selectedStartChanged();
}

void CastleExcellentSession::clearStart(int room) {
    if (placedStarts_.remove(room) == 0) {
        return;
    }
    emit startChanged();
    emit selectedStartChanged();
}

QString CastleExcellentSession::selectedStart() const {
    if (demoStart()) return tr("Title demo");
    const CastleExcellentStart start = this->start();
    return QStringLiteral("%1 · %2, %3").arg(roomLabel(start.room)).arg(start.pixel.x()).arg(start.pixel.y());
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> CastleExcellentSession::previewPalettes(const QString&, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const QVector<int> room = state_.graphics.tableValues(art_.roomPalette);
    for (int p = 0; p < 4; ++p) {
        const int at = (sprites ? 16 : 0) + p * 4;
        if (at + 4 <= room.size()) {
            result << nes::PreviewPalette{tr("Room %1 %2").arg(sprites ? QStringLiteral("SPR") : QStringLiteral("BG")).arg(p),
                                          room.mid(at, 4)};
        }
    }
    for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1"))) {
        if (id == art_.roomPalette) continue;
        const QVector<int> values = state_.graphics.tableValues(id);
        for (int at = 0; at + 4 <= values.size(); at += 4) {
            result << nes::PreviewPalette{tr("Screen %1").arg(at / 4), values.mid(at, 4)};
        }
    }
    return result;
}

QStringList CastleExcellentSession::paletteRowLabels(const QString& table) const {
    QStringList labels;
    if (table.startsWith(QLatin1String("screen:"))) {
        for (int r = 0; r < 8; ++r) labels << (r < 4 ? tr("BG %1") : tr("SPR %1")).arg(r % 4);
        return labels;
    }
    const int rows = state_.graphics.tableValues(table).size() / 4;
    for (int r = 0; r < rows; ++r) {
        const int set = r / 8;
        const QString kind = r % 8 < 4 ? tr("BG %1").arg(r % 4) : tr("SPR %1").arg(r % 4);
        labels << (rows > 8 ? tr("Set %1 · %2").arg(set).arg(kind) : kind);
    }
    return labels;
}

QVector<sdk::PaletteSample> CastleExcellentSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    if (table.startsWith(QLatin1String("screen:"))) {
        const QString id = table.mid(7);
        const QJsonArray list = state_.screens.value(QStringLiteral("screens")).toArray();
        const int index = screens::ids().indexOf(id);
        if (index >= 0 && index < list.size()) {
            sdk::PaletteSample sample;
            sample.label = screens::label(id);
            sample.caption = tr("The %1 screen, as the game shows it").arg(screens::label(id).toLower());
            sample.image = screens::render(this, list.at(index).toObject());
            result << sample;
        }
        return result;
    }
    if (table != art_.roomPalette || state_.levels.roomCount() == 0) {
        return result;
    }
    sdk::PaletteSample sample;
    sample.label = roomLabel(start_.room);
    sample.caption = tr("%1, as the game draws it").arg(roomLabel(start_.room));
    const int r = start_.room - 1;
    sample.image = painter().room(state_.levels.cells(r), state_.levels.objects(r));
    result << sample;
    return result;
}

// ---------------------------------------------------------------- build and play

void CastleExcellentSession::build() {
    runBuild([](bool) {});
}

void CastleExcellentSession::play() {
    launch(true);
}

void CastleExcellentSession::playSelected() {
    if (isRecording()) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launch(false);
}

QString CastleExcellentSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

// ---------------------------------------------------------------- music

QJsonObject CastleExcellentSession::music() const {
    return musicId_.isEmpty() ? QJsonObject() : state_.graphics.resource(musicId_);
}

QJsonObject CastleExcellentSession::savedMusic() const {
    return musicId_.isEmpty() ? QJsonObject() : saved_.graphics.resource(musicId_);
}

bool CastleExcellentSession::editMusic(const QString& text, const QJsonObject& next, QString* error) {
    if (musicId_.isEmpty()) return false;
    const QString id = musicId_;
    return editBundle(text, [id, next](nes::Bundle& b) { b.setResource(id, next); }, error);
}

// ---------------------------------------------------------------- demo

int CastleExcellentSession::demoCapacity() const {
    const int capacity = state_.demo.value(QStringLiteral("layout")).toObject().value(QStringLiteral("capacity")).toInt();
    return capacity > 0 ? capacity : 72;
}

bool CastleExcellentSession::editDemo(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error) {
    QJsonObject next = state_.demo;
    change(next);
    const QJsonArray steps = next.value(QStringLiteral("steps")).toArray();
    int commands = 0;
    for (const QJsonValue& step : steps) commands += step.toObject().value(QStringLiteral("commands")).toInt();
    if (steps.isEmpty() || commands > demoCapacity()) {
        if (error) *error = tr("The demo holds 1-%1 commands of 12 frames.").arg(demoCapacity());
        return false;
    }
    return edit(text, [next](CastleExcellentState& s) { s.demo = next; }, error);
}

void CastleExcellentSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) {
        emit startChanged();
        emit selectedStartChanged();
    }
}

void CastleExcellentSession::playDemo(bool record) {
    if (!isRecording()) launchDemo(record);
}

bool CastleExcellentSession::editScreens(const QString& text, const QJsonObject& next, QString* error) {
    if (state_.screens.isEmpty()) return false;
    return edit(text, [next](CastleExcellentState& s) { s.screens = next; }, error);
}

void CastleExcellentSession::upgradeDocuments(const QString& kind, const QString& document) {
    UpgradeStep step;
    step.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("castle-excellent-upgrade"), project_.path()};
    step.label = tr("Add the title demo and screens");
    step.source = kind;
    step.failure = tr("Adding the demo and screens failed: %1");
    step.adopt = [this] {
        // New documents join the saved state; other unsaved edits stay.
        const QString demo = tabDocument(project_, QStringLiteral("castle-excellent.demo@1"));
        if (!demo.isEmpty() && state_.demo.isEmpty()) {
            demoPath_ = project_.resolve(demo);
            state_.demo = saved_.demo = loadJsonObject(demoPath_);
        }
        const QString screensDocument = tabDocument(project_, QStringLiteral("castle-excellent.screens@1"));
        if (!screensDocument.isEmpty() && state_.screens.isEmpty()) {
            screensPath_ = project_.resolve(screensDocument);
            state_.screens = saved_.screens = loadJsonObject(screensPath_);
        }
    };
    step.done = tr("Added the title demo and the screens to the project.");
    step.openKind = kind;
    step.openDocument = document;
    runUpgrade(step);
}

void CastleExcellentSession::launchDemo(bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("castle-excellent-demo-plan"), project_.path()};
    plan.label = record ? tr("Plan demo recording") : tr("Plan title demo");
    const QString name = profile().name;
    plan.title = record ? tr("%1 · recording the demo").arg(name) : tr("%1 · title demo").arg(name);
    plan.demo = true;
    // The Playtest view covers the way to the demo.
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("CASTLE_EXCELLENT_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage = tr("Recording: play with the controller; it ends after %1 s, on Start or on Stop "
                                   "recording.")
                                    .arg(demoCapacity() * 12 / 60);
        plan.recorded = [this](const QString& path) { applyRecording(path); };
    }
    plan.prepare = [this](sdk::ProcessSpec& game) { useColours(game); };
    launchPlanned(plan);
}

void CastleExcellentSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const CastleExcellentDemoModel::Recording recording =
        CastleExcellentDemoModel::stepsFromRecording(QString::fromUtf8(file.readAll()), demoCapacity());
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
    report(sdk::Severity::Info, QStringLiteral("demo"),
           tr("Recorded %1 commands (%2 s) into the title demo's %3 steps.")
               .arg(recording.commands)
               .arg(recording.commands * 12 / 60.0, 0, 'f', 1)
               .arg(steps.size()));
}

void CastleExcellentSession::launch(bool fromStart) {
    const CastleExcellentStart start = this->start();
    const QString name = profile().name;
    EmulatorPlan plan;
    if (fromStart) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("%1 · power-on").arg(name);
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("castle-excellent-point-playtest-plan"), project_.path(),
                          QStringLiteral("--room"), QString::number(start.room), QStringLiteral("--x"),
                          QString::number(start.pixel.x()), QStringLiteral("--y"), QString::number(start.pixel.y() + 16)};
        plan.label = tr("Plan playtest of %1").arg(roomLabel(start.room));
        plan.title = QStringLiteral("%1 · %2").arg(name, roomLabel(start.room));
        // The Playtest view covers the title and the way to the room.
        plan.readyFile = QStringLiteral("build/playtest-ready.txt");
        plan.readyVariable = QStringLiteral("CASTLE_EXCELLENT_PLAYTEST_READY");
    }
    plan.prepare = [this](sdk::ProcessSpec& game) { useColours(game); };
    launchPlanned(plan);
}

void CastleExcellentSession::useColours(sdk::ProcessSpec& game) const {
    // The game in the colours the editors draw with.
    const std::string model = state_.graphics.paletteModel(art_.roomPalette).toStdString();
    for (int i = 0; i < 64; ++i) {
        game.palette << nes::masterColor(i, model);
    }
}

}  // namespace rl::castle_excellent
