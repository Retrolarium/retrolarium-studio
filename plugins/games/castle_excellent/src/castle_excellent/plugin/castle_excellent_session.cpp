#include "castle/plugin/castle_session.hpp"

#include "castle/plugin/animation_model.hpp"
#include "castle/plugin/cell_model.hpp"
#include "castle/plugin/demo_model.hpp"
#include "castle/plugin/map_model.hpp"
#include "castle/plugin/music_models.hpp"
#include "castle/plugin/room_model.hpp"
#include "castle/plugin/screen_model.hpp"
#include "nes/sound_effect_model.hpp"
#include "castle/plugin/sprite_model.hpp"
#include "castle/plugin/table_models.hpp"
#include "nes/palette.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUndoStack>

#include <algorithm>

namespace rl::castle {

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

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta = QString(), bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
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
    return rlTr("castle", "exit code %1").arg(result.exitCode);
}

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

QJsonObject loadJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw CastleError(rlTr("castle", "Cannot read %1.").arg(path).toStdString());
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) throw CastleError(rlTr("castle", "%1 is not a JSON object.").arg(path).toStdString());
    return doc.object();
}

void saveJsonObject(const QString& path, const QJsonObject& value) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw CastleError(rlTr("castle", "Cannot write %1.").arg(path).toStdString());
    file.write(QJsonDocument(value).toJson(QJsonDocument::Indented));
    if (!file.commit()) throw CastleError(rlTr("castle", "Cannot write %1.").arg(path).toStdString());
}

QString first(const nes::Bundle& bundle, const char* codec) {
    const QStringList ids = bundle.resourceIds(QString::fromLatin1(codec));
    return ids.isEmpty() ? QString() : ids.first();
}

// The five start routines of the sound effect engine ($C20D-$C2BD); each
// is played by UpdateSoundEffects ($C2F9) from the next frame.
QVector<nes::SoundEffect> soundEffects() {
    const QString tuned = rlTr("castle", "Its tunable bytes are in SFX parameters.");
    return {{QStringLiteral("castle:1"), rlTr("castle", "Jump"), rlTr("castle", "Start routine $C20D"), QString()},
            {QStringLiteral("castle:2"), rlTr("castle", "Crush"),
             rlTr("castle", "Start routine $C233"), QString()},
            {QStringLiteral("castle:3"), rlTr("castle", "Item collected"),
             rlTr("castle", "Start routine $C272"), tuned},
            {QStringLiteral("castle:4"), rlTr("castle", "Flame"), rlTr("castle", "Start routine $C29A"), tuned},
            {QStringLiteral("castle:5"), rlTr("castle", "Cell opens"),
             rlTr("castle", "Start routine $C2BD"), QString()}};
}

}  // namespace

QString roomLabel(int room) {
    return rlTr("castle", "Room %1").arg(room);
}

// ---------------------------------------------------------------- opening

std::unique_ptr<CastleSession> CastleSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<CastleSession> session(new CastleSession);
    session->project_ = project;
    try {
        const QString levels = tabDocument(project, QStringLiteral("castle.prefix-rooms@1"));
        const QString graphics = tabDocument(project, QStringLiteral("nes.chr-2bpp.v1"));
        if (levels.isEmpty() || graphics.isEmpty()) {
            throw CastleError("The project has no Castle rooms or graphics tab.");
        }
        session->levelsPath_ = project.resolve(levels);
        session->graphicsPath_ = project.resolve(graphics);
        session->state_.levels = CastleLevels::load(session->levelsPath_);
        session->state_.graphics = nes::Bundle::load(session->graphicsPath_);
        const QString demo = tabDocument(project, QStringLiteral("castle.demo@1"));
        if (!demo.isEmpty()) {
            session->demoPath_ = project.resolve(demo);
            session->state_.demo = loadJsonObject(session->demoPath_);
        }
        const QString screensDocument = tabDocument(project, QStringLiteral("castle.screens@1"));
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
        session->art_.cellVisuals = first(g, "castle.room-cell-visuals.v1");
        session->art_.descriptors = first(g, "castle.metasprite-descriptors.v1");
        session->musicId_ = first(g, "castle.mml-music.v1");
        if (session->art_.chr.isEmpty() || session->art_.roomPalette.isEmpty() ||
            g.tableValues(session->art_.roomPalette).size() < 32) {
            throw CastleError("The graphics bundle lacks the room patterns or palette.");
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

sdk::Profile CastleSession::profile() const {
    sdk::Profile profile;
    const bool usa = state_.levels.revision().contains(QLatin1String("usa"));
    profile.name = usa ? tr("Castlequest") : tr("Castle Excellent");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    profile.revision = usa ? QStringLiteral("U") : QStringLiteral("J");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("castle.prefix-rooms@1");
    return profile;
}

QVector<sdk::Kind> CastleSession::kinds() const {
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

QVector<sdk::DocumentGroup> CastleSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("rooms")) {
        sdk::DocumentGroup overview;
        overview.title = tr("Overview");
        overview.documents << entry(QStringLiteral("map"), tr("Castle map"), QStringLiteral("10 × 10"));
        groups << overview;
        sdk::DocumentGroup group;
        group.title = tr("Castle");
        for (int r = 0; r < state_.levels.roomCount(); ++r) {
            const QVector<CastleObject> objects = state_.levels.objects(r);
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
        const QVector<CastleAnimation>& animations = castleAnimations();
        for (int i = 0; i < animations.size(); ++i) {
            const CastleAnimation& a = animations.at(i);
            bool dirty = false;
            for (const CastleAnimationFrame& f : a.frames) {
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
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle.sub-command-text.v1"))) {
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
                                QStringLiteral("%1 B").arg(CastleMusic::usedBytes(now)),
                                now.value(QStringLiteral("streams")) != saved.value(QStringLiteral("streams")));
        sdk::DocumentGroup effects;
        effects.title = tr("Sound effects");
        for (const nes::SoundEffect& effect : soundEffects()) {
            effects.documents << entry(QStringLiteral("sfx:") + effect.key, effect.title, QString());
        }
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle.sfx-parameters.v1"))) {
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
                                 lastBuild_.isEmpty() ? QString() : QStringLiteral("built"));
        groups << group;
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> CastleSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("rooms") && document == QLatin1String("map")) {
            return std::make_unique<CastleMapModel>(this);
        }
        if (kind == QLatin1String("rooms")) {
            const int room = document.toInt() - 1;
            if (room >= 0 && room < state_.levels.roomCount()) {
                return std::make_unique<CastleRoomModel>(this, room);
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
            return std::make_unique<CastleScreenModel>(this, screens::ids().indexOf(document));
        }
        if (kind == QLatin1String("text") && document == QLatin1String("ending")) {
            return std::make_unique<CastleScreenTextModel>(this);
        }
        if (kind == QLatin1String("text") &&
            state_.graphics.resourceIds(QStringLiteral("castle.sub-command-text.v1")).contains(document)) {
            return std::make_unique<CastleTextModel>(this, document);
        }
        if (kind == QLatin1String("metatiles") && document == QLatin1String("cells") && !art_.cellVisuals.isEmpty()) {
            return std::make_unique<CastleCellModel>(this);
        }
        if (kind == QLatin1String("animations") && document.startsWith(QLatin1String("anim:")) && !art_.descriptors.isEmpty()) {
            const int index = document.mid(5).toInt();
            if (index >= 0 && index < castleAnimations().size()) return std::make_unique<CastleAnimationModel>(this, index);
        }
        if (kind == QLatin1String("animations") && document == QLatin1String("sprites") && !art_.descriptors.isEmpty()) {
            return std::make_unique<CastleSpriteModel>(this);
        }
        if (kind == QLatin1String("sound") && document.startsWith(QLatin1String("sfx:"))) {
            const QStringList watched = state_.graphics.resourceIds(QStringLiteral("castle.sfx-parameters.v1"));
            for (const nes::SoundEffect& effect : soundEffects()) {
                if (QStringLiteral("sfx:") + effect.key == document) {
                    return std::make_unique<nes::SoundEffectModel>(this, effect, project_.path(), project_.directory(),
                                                                   nes::bundleSoundEffectData(this, watched));
                }
            }
        }
        if (kind == QLatin1String("sound") &&
            state_.graphics.resourceIds(QStringLiteral("castle.sfx-parameters.v1")).contains(document)) {
            return std::make_unique<CastleSfxModel>(this, document);
        }
        if (kind == QLatin1String("sound") && !musicId_.isEmpty()) {
            if (document == QLatin1String("streams")) return std::make_unique<CastleStreamsModel>(this);
            if (document.startsWith(QLatin1String("stream:"))) {
                const int stream = document.mid(7).toInt();
                if (stream >= 0 && stream < music().value(QStringLiteral("streams")).toArray().size()) {
                    return std::make_unique<CastleSequenceModel>(this, stream);
                }
            }
            const int track = document.startsWith(QLatin1String("track:")) ? document.mid(6).toInt() : -1;
            if (track >= 0 && track < music().value(QStringLiteral("track_sets")).toArray().size()) {
                return std::make_unique<CastleMusicModel>(this, track);
            }
        }
        if (kind == QLatin1String("demo")) {
            if (document == QLatin1String("demo") && state_.demo.isEmpty()) {
                upgradeDocuments(kind, document);
                return nullptr;
            }
            if (document == QLatin1String("demo") && !state_.demo.isEmpty()) {
                return std::make_unique<CastleDemoModel>(this);
            }
        }
        if (kind == QLatin1String("playtest")) {
            return std::make_unique<CastlePlaytestModel>(this);
        }
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const CastlePainter& CastleSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<CastlePainter>(state_.levels, state_.graphics, art_);
        painterRevision_ = revision_;
    }
    return *painter_;
}

void CastleSession::setState(const CastleState& state, quint64 revision) {
    const CastleState current = state_;
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

bool CastleSession::edit(const QString& text, const std::function<void(CastleState&)>& change, QString* error) {
    CastleState next = state_;
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
    undoStack()->push(new sdk::SnapshotCommand<CastleState>(
        [this](const CastleState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, text));
    return true;
}

bool CastleSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change,
                               QString* error) {
    return edit(text, [change](CastleState& s) { change(s.graphics); }, error);
}

bool CastleSession::save(QString* error) {
    try {
        if (state_.levels != saved_.levels) {
            state_.levels.save(levelsPath_);
        }
        if (state_.graphics != saved_.graphics) {
            state_.graphics.save(graphicsPath_);
        }
        if (state_.demo != saved_.demo && !demoPath_.isEmpty()) {
            saveJsonObject(demoPath_, state_.demo);
        }
        if (state_.screens != saved_.screens && !screensPath_.isEmpty()) {
            saveJsonObject(screensPath_, state_.screens);
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

void CastleSession::setStart(const CastleStart& start) {
    CastleStart next = start;
    next.room = std::clamp(next.room, 1, std::max(1, state_.levels.roomCount()));
    // The player's top-left; the game keeps its feet, 16 pixels lower.
    next.pixel = QPoint(std::clamp(next.pixel.x(), 0, 304), std::clamp(next.pixel.y(), 0, 143));
    if (next.room == start_.room && next.pixel == start_.pixel) {
        return;
    }
    start_ = next;
    emit startChanged();
    emit selectedStartChanged();
}

QString CastleSession::selectedStart() const {
    if (demoStart()) return tr("Title demo");
    return QStringLiteral("%1 · %2, %3").arg(roomLabel(start_.room)).arg(start_.pixel.x()).arg(start_.pixel.y());
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> CastleSession::previewPalettes(const QString&, bool sprites) const {
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

QStringList CastleSession::paletteRowLabels(const QString& table) const {
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

QVector<sdk::PaletteSample> CastleSession::paletteSamples(const QString& table) const {
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

void CastleSession::report(sdk::Severity severity, const QString& source, const QString& message) {
    if (!host()) {
        return;
    }
    sdk::Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = message;
    d.location = project_.path().section(QLatin1Char('/'), -1);
    host()->report(d);
}

void CastleSession::runBuild(std::function<void(bool)> next) {
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
    spec.label = tr("Build ROM");
    h->runProcess(spec, [this, next](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, QStringLiteral("build"),
                       tr("Build failed: %1").arg(failureText(result)));
            }
            next(false);
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        const QJsonObject artifact = json.value(QStringLiteral("artifact")).toObject();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("built")) {
            report(sdk::Severity::Error, QStringLiteral("build"), tr("The kit did not report a built ROM."));
            next(false);
            return;
        }
        lastBuild_ = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        report(sdk::Severity::Info, QStringLiteral("build"),
               tr("Built %1 (%2 bytes).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt()));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void CastleSession::build() {
    runBuild([](bool) {});
}

void CastleSession::play() {
    launch(true);
}

void CastleSession::playSelected() {
    if (recordingActive_) stopRecording();
    else if (demoStart()) launchDemo(false);
    else launch(false);
}

QString CastleSession::playSelectedLabel() const {
    if (recordingActive_) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

// ---------------------------------------------------------------- music

QJsonObject CastleSession::music() const {
    return musicId_.isEmpty() ? QJsonObject() : state_.graphics.resource(musicId_);
}

QJsonObject CastleSession::savedMusic() const {
    return musicId_.isEmpty() ? QJsonObject() : saved_.graphics.resource(musicId_);
}

bool CastleSession::editMusic(const QString& text, const QJsonObject& next, QString* error) {
    if (musicId_.isEmpty()) return false;
    const QString id = musicId_;
    return editBundle(text, [id, next](nes::Bundle& b) { b.setResource(id, next); }, error);
}

// ---------------------------------------------------------------- demo

int CastleSession::demoCapacity() const {
    const int capacity = state_.demo.value(QStringLiteral("layout")).toObject().value(QStringLiteral("capacity")).toInt();
    return capacity > 0 ? capacity : 72;
}

bool CastleSession::editDemo(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error) {
    QJsonObject next = state_.demo;
    change(next);
    const QJsonArray steps = next.value(QStringLiteral("steps")).toArray();
    int commands = 0;
    for (const QJsonValue& step : steps) commands += step.toObject().value(QStringLiteral("commands")).toInt();
    if (steps.isEmpty() || commands > demoCapacity()) {
        if (error) *error = tr("The demo holds 1-%1 commands of 12 frames.").arg(demoCapacity());
        return false;
    }
    return edit(text, [next](CastleState& s) { s.demo = next; }, error);
}

void CastleSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) {
        emit startChanged();
        emit selectedStartChanged();
    }
}

void CastleSession::playDemo(bool record) {
    if (!recordingActive_) launchDemo(record);
}

void CastleSession::stopRecording() {
    if (recordingActive_ && host()) host()->stopEmulator();
}

bool CastleSession::editScreens(const QString& text, const QJsonObject& next, QString* error) {
    if (state_.screens.isEmpty()) return false;
    return edit(text, [next](CastleState& s) { s.screens = next; }, error);
}

void CastleSession::upgradeDocuments(const QString& kind, const QString& document) {
    sdk::Host* h = host();
    if (!h || h->isBusy()) return;
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("castle-upgrade"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    spec.label = tr("Add the title demo and screens");
    h->runProcess(spec, [this, kind, document](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, kind,
                       tr("Adding the demo and screens failed: %1").arg(failureText(result)));
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
            // New documents join the saved state; other unsaved edits stay.
            const QString demo = tabDocument(project_, QStringLiteral("castle.demo@1"));
            if (!demo.isEmpty() && state_.demo.isEmpty()) {
                demoPath_ = project_.resolve(demo);
                state_.demo = saved_.demo = loadJsonObject(demoPath_);
            }
            const QString screensDocument = tabDocument(project_, QStringLiteral("castle.screens@1"));
            if (!screensDocument.isEmpty() && state_.screens.isEmpty()) {
                screensPath_ = project_.resolve(screensDocument);
                state_.screens = saved_.screens = loadJsonObject(screensPath_);
            }
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
            return;
        }
        report(sdk::Severity::Info, kind, tr("Added the title demo and the screens to the project."));
        emit contentChanged();
        for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
        emit documentRequested(kind, document);
    });
}

void CastleSession::launchDemo(bool record) {
    runBuild([this, record](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) return;
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   tr("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QStringList arguments{QStringLiteral("studio"), QStringLiteral("castle-demo-plan"), project_.path(),
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
        plan.label = record ? tr("Plan demo recording") : tr("Plan title demo");
        h->runProcess(plan, [this, record, recording](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) return;
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           tr("Demo plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), tr("The kit returned no emulator command."));
                return;
            }
            const std::string model = state_.graphics.paletteModel(art_.roomPalette).toStdString();
            for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i, model);
            // The Playtest view covers the way to the demo.
            game.readyFile = project_.resolve(QStringLiteral("build/demo-ready.txt"));
            game.environment.insert(QStringLiteral("CASTLE_DEMO_READY"), game.readyFile);
            if (record) {
                recordingActive_ = true;
                emit selectedStartChanged();
                report(sdk::Severity::Info, QStringLiteral("demo"),
                       tr("Recording: play with the controller; it ends after %1 s, on Start or on Stop "
                          "recording.")
                           .arg(demoCapacity() * 12 / 60));
            }
            const QString name = profile().name;
            const QString title = record ? tr("%1 · recording the demo").arg(name)
                                         : tr("%1 · title demo").arg(name);
            host->launchEmulator(game, title, [this, record, recording](const sdk::ProcessResult& run) {
                if (run.crashed || (!run.started && !run.error.isEmpty())) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           run.error.isEmpty() ? tr("The emulator crashed.") : run.error);
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

void CastleSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const CastleDemoModel::Recording recording =
        CastleDemoModel::stepsFromRecording(QString::fromUtf8(file.readAll()), demoCapacity());
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

void CastleSession::launch(bool fromStart) {
    const CastleStart start = start_;
    runBuild([this, fromStart, start](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) {
            return;
        }
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   tr("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QStringList arguments;
        if (fromStart) {
            arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path(),
                                    QStringLiteral("--mame"), mame};
        } else {
            arguments = QStringList{QStringLiteral("studio"), QStringLiteral("castle-point-playtest-plan"),
                                    project_.path(), QStringLiteral("--mame"), mame, QStringLiteral("--room"),
                                    QString::number(start.room), QStringLiteral("--x"),
                                    QString::number(start.pixel.x()), QStringLiteral("--y"),
                                    QString::number(start.pixel.y() + 16)};
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        const QString name = profile().name;
        const QString title = fromStart ? tr("%1 · power-on").arg(name)
                                        : QStringLiteral("%1 · %2").arg(name, roomLabel(start.room));
        plan.label = fromStart ? tr("Plan playtest from power-on")
                               : tr("Plan playtest of %1").arg(roomLabel(start.room));
        h->runProcess(plan, [this, title, fromStart](const sdk::ProcessResult& result) {
            sdk::Host* host = this->host();
            if (!host) {
                return;
            }
            if (!result.succeeded()) {
                if (!result.cancelled) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           tr("Playtest plan failed: %1").arg(failureText(result)));
                }
                return;
            }
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), tr("The kit returned no emulator command."));
                return;
            }
            const std::string model = state_.graphics.paletteModel(art_.roomPalette).toStdString();
            for (int i = 0; i < 64; ++i) {
                game.palette << nes::masterColor(i, model);
            }
            if (!fromStart) {
                // The Playtest view covers the title and the way to the room.
                game.readyFile = project_.resolve(QStringLiteral("build/playtest-ready.txt"));
                game.environment.insert(QStringLiteral("CASTLE_PLAYTEST_READY"), game.readyFile);
            }
            host->launchEmulator(game, title, [this](const sdk::ProcessResult& run) {
                if (run.crashed || (!run.started && !run.error.isEmpty())) {
                    report(sdk::Severity::Error, QStringLiteral("playtest"),
                           run.error.isEmpty() ? tr("The emulator crashed.") : run.error);
                }
            });
        });
    });
}

}  // namespace rl::castle
