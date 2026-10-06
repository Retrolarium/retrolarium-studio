#include "solomon/plugin/solomon_session.hpp"

#include "nes/palette.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"
#include "solomon/plugin/animation_model.hpp"
#include "solomon/plugin/mirror_models.hpp"
#include "solomon/plugin/palette_model.hpp"
#include "solomon/plugin/presentation_models.hpp"
#include "solomon/plugin/room_model.hpp"
#include "solomon/plugin/sound_model.hpp"
#include "solomon/plugin/text_models.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QUndoStack>

#include <algorithm>

namespace rl::solomon {

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
    return rlTr("solomon", "exit code %1").arg(result.exitCode);
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

// Rooms 1-48 come in twelve worlds of four; 49-53 are the bonus, princess
// and other special rooms.
QString worldOf(int room) {
    return room < 48 ? rlTr("solomon", "World %1").arg(room / 4 + 1) : rlTr("solomon", "Special rooms");
}

}  // namespace

QString roomLabel(int room) {
    return rlTr("solomon", "Room %1").arg(room);
}

// ---------------------------------------------------------------- opening

std::unique_ptr<SolomonSession> SolomonSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<SolomonSession> session(new SolomonSession);
    session->project_ = project;
    try {
        const QString rooms = tabDocument(project, QStringLiteral("solomon.rooms@3"));
        const QString art = tabDocument(project, QStringLiteral("solomon.room-art@1"));
        const QString animations = tabDocument(project, QStringLiteral("solomon.animations@1"));
        const QString graphics = tabDocument(project, QStringLiteral("nes.chr-2bpp.v1"));
        if (rooms.isEmpty() || art.isEmpty() || animations.isEmpty() || graphics.isEmpty()) {
            throw SolomonError("The project lacks the rooms, room art, animations or graphics tab.");
        }
        session->roomsPath_ = project.resolve(rooms);
        session->artPath_ = project.resolve(art);
        session->animationsPath_ = project.resolve(animations);
        session->graphicsPath_ = project.resolve(graphics);
        session->state_.rooms = SolomonRooms(loadJson(session->roomsPath_));
        session->state_.art = loadJson(session->artPath_);
        session->state_.animations = loadJson(session->animationsPath_);
        session->state_.graphics = nes::Bundle::load(session->graphicsPath_);
        // The title screen and attract demo; older projects may lack them.
        const QString presentation = tabDocument(project, QStringLiteral("solomon.presentation@1"));
        if (!presentation.isEmpty()) {
            session->presentationPath_ = project.resolve(presentation);
            session->state_.presentation = loadJson(session->presentationPath_);
        }
        const QString audio = tabDocument(project, QStringLiteral("solomon.audio@1"));
        if (!audio.isEmpty()) {
            session->audioPath_ = project.resolve(audio);
            session->state_.audio = loadJson(session->audioPath_);
        }
        const QString text = tabDocument(project, QStringLiteral("solomon.text@1"));
        if (!text.isEmpty()) {
            session->textPath_ = project.resolve(text);
            session->state_.text = SolomonText(loadJson(session->textPath_));
        }
        if (session->state_.rooms.root().value(QStringLiteral("schema")).toString() !=
                QLatin1String("retrolarium.solomon-rooms") ||
            session->state_.rooms.roomCount() == 0) {
            throw SolomonError("The rooms document is not a Solomon's Key rooms document.");
        }
        if (!session->state_.graphics.contains(QStringLiteral("room-palettes"))) {
            throw SolomonError("The graphics bundle has no room palettes.");
        }
        session->saved_ = session->state_;
        // A demo step of 255 frames stalls the game's demo (its counter never
        // passes 255) and the kit refuses it; recordings made before that
        // was known have one. Open them at 254 as an edit, so the next build
        // saves it.
        const auto clamp = [](SolomonState& s) {
            QJsonArray steps = s.presentation.value(QStringLiteral("demo_steps")).toArray();
            for (int i = 0; i < steps.size(); ++i) {
                QJsonObject step = steps.at(i).toObject();
                if (step.value(QStringLiteral("duration")).toInt() > 254) {
                    step.insert(QStringLiteral("duration"), 254);
                    steps.replace(i, step);
                }
            }
            if (!steps.isEmpty()) s.presentation.insert(QStringLiteral("demo_steps"), steps);
        };
        session->edit(tr("Shorten a 255-frame demo step to 254"), clamp, nullptr);
        session->painter();
    } catch (const std::exception& e) {
        if (error) {
            *error = tr("Cannot open the Solomon's Key project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile SolomonSession::profile() const {
    sdk::Profile profile;
    profile.name = tr("Solomon's Key");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    const QString revision = state_.rooms.revision();
    profile.revision = revision == QLatin1String("europe")  ? QStringLiteral("E")
                       : revision == QLatin1String("japan") ? QStringLiteral("J")
                                                            : QStringLiteral("U");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("solomon.rooms@3");
    return profile;
}

QVector<sdk::Kind> SolomonSession::kinds() const {
    // The Spawn Mirrors' enemy sets and drop schedules list under Levels.
    return {kind("rooms", "Levels", "levels", sdk::EditorType::TileMap),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("animations", "Animations", "sprites", sdk::EditorType::Composition),
            kind("screens", "Screens", "tileset", sdk::EditorType::TileMap),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sound", "Sound", "sound", sdk::EditorType::Sequencer),
            kind("demo", "Demo", "play", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> SolomonSession::documents(const QString& kind) const {
    auto found = documents_.constFind(kind);
    if (found != documents_.constEnd()) return *found;
    return *documents_.insert(kind, listDocuments(kind));
}

QVector<sdk::DocumentGroup> SolomonSession::listDocuments(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("rooms")) {
        const QJsonArray now = state_.rooms.root().value(QStringLiteral("rooms")).toArray();
        const QJsonArray saved = saved_.rooms.root().value(QStringLiteral("rooms")).toArray();
        for (int r = 0; r < state_.rooms.roomCount(); ++r) {
            const QString title = worldOf(r);
            if (groups.isEmpty() || groups.last().title != title) {
                sdk::DocumentGroup group;
                group.title = title;
                groups << group;
            }
            const int enemies = state_.rooms.enemies(r).size();
            groups.last().documents << entry(QString::number(r + 1), roomLabel(state_.rooms.roomNumber(r)),
                                             enemies ? tr("%1 enemies").arg(enemies) : QString(),
                                             now.at(r) != saved.at(r));
        }
        sdk::DocumentGroup group;
        group.title = tr("Spawn Mirrors");
        group.documents << entry(QStringLiteral("sets"), tr("Enemy sets"),
                                 QStringLiteral("%1 / %2 B").arg(state_.rooms.mirrorSetBytes()).arg(state_.rooms.mirrorSetCapacity()),
                                 state_.rooms.mirrorSets() != saved_.rooms.mirrorSets())
                        << entry(QStringLiteral("schedules"), tr("Drop schedules"),
                                 QStringLiteral("%1").arg(state_.rooms.schedules().size()),
                                 state_.rooms.schedules() != saved_.rooms.schedules());
        groups << group;
    } else if (kind == QLatin1String("screens") && !state_.presentation.isEmpty()) {
        sdk::DocumentGroup group;
        group.title = tr("Title screen");
        const QJsonArray now = state_.presentation.value(QStringLiteral("streams")).toArray();
        const QJsonArray saved = saved_.presentation.value(QStringLiteral("streams")).toArray();
        group.documents << entry(QStringLiteral("0"), SolomonTitleModel::layerName(0), QStringLiteral("155 B"), now.at(0) != saved.at(0))
                        << entry(QStringLiteral("1"), SolomonTitleModel::layerName(1), QStringLiteral("247 B"), now.at(1) != saved.at(1));
        groups << group;
        if (!state_.text.isEmpty()) {
            sdk::DocumentGroup text;
            text.title = QStringLiteral("Text screens");
            const bool dirty = !(state_.text == saved_.text);
            for (const TextScreen& screen : textScreens()) {
                text.documents << entry(QStringLiteral("text:%1").arg(screen.id), screen.label, QString(), dirty);
            }
            groups << text;
        }
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        if (state_.text.isEmpty()) {
            group.documents << entry(QStringLiteral("upgrade"), tr("Add screen text to this project"),
                                     QStringLiteral("kit"));
        } else {
            group.documents << entry(QStringLiteral("all"), tr("All text"),
                                     tr("%1 lines").arg(state_.text.strings().size()),
                                     !(state_.text == saved_.text));
        }
        groups << group;
    } else if (kind == QLatin1String("sound") && !state_.audio.isEmpty()) {
        const QJsonArray now = state_.audio.value(QStringLiteral("commands")).toArray();
        const QJsonArray saved = saved_.audio.value(QStringLiteral("commands")).toArray();
        for (const auto& programs : SolomonSoundModel::programGroups()) {
            sdk::DocumentGroup group;
            group.title = programs.first;
            for (int program : programs.second) {
                bool dirty = false;
                const auto notes = SolomonSoundModel::trace(state_.audio, program, 60 * 40);
                for (const auto& list : notes) {
                    for (const SolomonNote& note : list) dirty = dirty || now.at(note.command) != saved.at(note.command);
                }
                group.documents << entry(QString::number(program), SolomonSoundModel::programLabel(program).section(QStringLiteral(" · "), 1),
                                         QStringLiteral("#%1").arg(program, 2, 10, QLatin1Char('0')), dirty);
            }
            groups << group;
        }
    } else if (kind == QLatin1String("demo") && !state_.presentation.isEmpty()) {
        sdk::DocumentGroup group;
        group.title = tr("Title");
        group.documents << entry(QStringLiteral("demo"), tr("Attract demo"),
                                 tr("%1 steps").arg(state_.presentation.value(QStringLiteral("demo_steps")).toArray().size()),
                                 state_.presentation.value(QStringLiteral("demo_steps")) !=
                                     saved_.presentation.value(QStringLiteral("demo_steps")));
        groups << group;
    } else if (kind == QLatin1String("animations")) {
        const QJsonArray frames = state_.animations.value(QStringLiteral("frames")).toArray();
        const QJsonArray savedFrames = saved_.animations.value(QStringLiteral("frames")).toArray();
        for (const AnimationFamily& family : animationFamilies()) {
            const QString title = SolomonAnimationModel::groupLabel(family.group);
            if (groups.isEmpty() || groups.last().title != title) {
                sdk::DocumentGroup group;
                group.title = title;
                groups << group;
            }
            bool dirty = false;
            for (const AnimationMember& m : family.members) {
                dirty = dirty || SolomonAnimationModel::descriptor(state_.animations, family.group, m.action) !=
                                     SolomonAnimationModel::descriptor(saved_.animations, family.group, m.action);
                for (int index : SolomonAnimationModel::cycle(state_.animations, family.group, m.action, 0)) {
                    dirty = dirty || frames.at(index) != savedFrames.at(index);
                }
            }
            const int first = family.members.first().action;
            groups.last().documents << entry(QStringLiteral("%1:%2").arg(family.group).arg(first),
                                             SolomonAnimationModel::familyLabel(family),
                                             family.frames == 1 ? tr("1 frame")
                                                                : tr("%1 frames").arg(family.frames),
                                             dirty);
        }
    } else if (kind == QLatin1String("graphics")) {
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1"))) {
            sdk::DocumentGroup group;
            group.title = id;
            const int count = state_.graphics.tileCount(id);
            const bool changed = state_.graphics.resource(id) != saved_.graphics.resource(id);
            for (int half = 0; half * 256 < count; ++half) {
                bool dirty = false;
                for (int t = half * 256; changed && t < std::min(count, half * 256 + 256) && !dirty; ++t) {
                    dirty = state_.graphics.tilePixels(id, t) != saved_.graphics.tilePixels(id, t);
                }
                group.documents << entry(QStringLiteral("%1/%2").arg(id).arg(half), tr("Pattern table %1").arg(half),
                                         half == 0 ? QStringLiteral("sprites") : QStringLiteral("rooms"), dirty);
            }
            // The 8x16 pairs sprite halves are drawn from, in either table.
            for (int half = 0; half * 256 < count; ++half) {
                group.documents << entry(QStringLiteral("%1/pairs%2").arg(id).arg(half),
                                         tr("Sprite pairs · table %1").arg(half), QStringLiteral("8x16"), false);
            }
            groups << group;
        }
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup rooms;
        rooms.title = tr("Rooms");
        const QVector<int> nowTemplate = state_.graphics.tableValues(QStringLiteral("room-palettes"));
        const QVector<int> savedTemplate = saved_.graphics.tableValues(QStringLiteral("room-palettes"));
        for (int g = 0; g < SolomonRoomPaletteModel::kGroups; ++g) {
            const int colour = groupColor(state_.art, g);
            rooms.documents << entry(QStringLiteral("group:%1").arg(g), SolomonRoomPaletteModel::groupLabel(g),
                                     colour >= 0x80 ? QStringLiteral("fixed") : nes::hex(colour),
                                     nowTemplate != savedTemplate || colour != groupColor(saved_.art, g));
        }
        groups << rooms;
        if (!state_.text.isEmpty()) {
            sdk::DocumentGroup screens;
            screens.title = tr("Screens");
            for (int program : {1, 5, 13}) {
                screens.documents << entry(QStringLiteral("static:%1").arg(program),
                                           SolomonStaticPaletteModel::programLabel(program), tr("16 colours"),
                                           state_.text.palette(program) != saved_.text.palette(program));
            }
            groups << screens;
        }
        sdk::DocumentGroup group;
        group.title = tr("Other");
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1"))) {
            if (id == QLatin1String("room-palettes")) continue;
            group.documents << entry(id,
                                     id == QLatin1String("room-palettes") ? tr("Rooms")
                                     : id == QLatin1String("ending-fade") ? tr("Ending fade")
                                                                          : id,
                                     tr("%1 colours").arg(state_.graphics.tableValues(id).size()),
                                     state_.graphics.tableValues(id) != saved_.graphics.tableValues(id));
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

std::unique_ptr<sdk::EditorModel> SolomonSession::openEditor(const QString& kind, const QString& document) {
    try {
        if (kind == QLatin1String("rooms")) {
            const int room = document.toInt() - 1;
            if (room >= 0 && room < state_.rooms.roomCount()) {
                return std::make_unique<SolomonRoomModel>(this, room);
            }
        }
        if (kind == QLatin1String("screens") && document.startsWith(QLatin1String("text:")) && !state_.text.isEmpty()) {
            for (const TextScreen& screen : textScreens()) {
                if (screen.id == document.mid(5)) return std::make_unique<SolomonTextScreenModel>(this, screen);
            }
        }
        if (kind == QLatin1String("text")) {
            if (document == QLatin1String("upgrade") && state_.text.isEmpty()) {
                upgradeText();
                return nullptr;
            }
            if (!state_.text.isEmpty()) return std::make_unique<SolomonTextTableModel>(this);
        }
        if (kind == QLatin1String("palettes") && document.startsWith(QLatin1String("static:")) && !state_.text.isEmpty()) {
            const int program = document.mid(7).toInt();
            if (program == 1 || program == 5 || program == 13) return std::make_unique<SolomonStaticPaletteModel>(this, program);
        }
        if (kind == QLatin1String("screens") && !state_.presentation.isEmpty() &&
            (document == QLatin1String("0") || document == QLatin1String("1"))) {
            return std::make_unique<SolomonTitleModel>(this, document.toInt());
        }
        if (kind == QLatin1String("sound") && !state_.audio.isEmpty()) {
            const int program = document.toInt();
            if (program >= 1 && program <= state_.audio.value(QStringLiteral("effects")).toArray().size()) {
                return std::make_unique<SolomonSoundModel>(this, program);
            }
        }
        if (kind == QLatin1String("demo") && !state_.presentation.isEmpty()) {
            return std::make_unique<SolomonDemoModel>(this);
        }
        if (kind == QLatin1String("animations")) {
            const int group = document.section(QLatin1Char(':'), 0, 0).toInt();
            const int action = document.section(QLatin1Char(':'), 1).toInt();
            if (!SolomonAnimationModel::descriptor(state_.animations, group, action).isEmpty()) {
                return std::make_unique<SolomonAnimationModel>(this, group, action);
            }
        }
        if (kind == QLatin1String("rooms")) {
            if (document == QLatin1String("sets")) return std::make_unique<SolomonMirrorSetsModel>(this);
            if (document == QLatin1String("schedules")) return std::make_unique<SolomonSchedulesModel>(this);
        }
        if (kind == QLatin1String("graphics") && document.section(QLatin1Char('/'), 1).startsWith(QLatin1String("pairs"))) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const int half = document.section(QLatin1Char('/'), 1).mid(5).toInt();
            if (state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1")).contains(id) &&
                half * 256 < state_.graphics.tileCount(id)) {
                return std::make_unique<nes::ChrTileModel>(
                    this, id, half * 256, std::min(256, state_.graphics.tileCount(id) - half * 256),
                    tr("%1 · pairs of table %2").arg(id).arg(half), true, 16);
            }
        }
        if (kind == QLatin1String("graphics")) {
            const QString id = document.section(QLatin1Char('/'), 0, 0);
            const int half = document.section(QLatin1Char('/'), 1).toInt();
            if (state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1")).contains(id) &&
                half * 256 < state_.graphics.tileCount(id)) {
                return std::make_unique<nes::ChrTileModel>(
                    this, id, half * 256, std::min(256, state_.graphics.tileCount(id) - half * 256),
                    tr("%1 · table %2").arg(id).arg(half), half == 0);
            }
        }
        if (kind == QLatin1String("palettes") && document.startsWith(QLatin1String("group:"))) {
            const int g = document.mid(6).toInt();
            if (g >= 0 && g < SolomonRoomPaletteModel::kGroups) return std::make_unique<SolomonRoomPaletteModel>(this, g);
        }
        if (kind == QLatin1String("palettes") &&
            state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1")).contains(document)) {
            return std::make_unique<nes::PaletteTableModel>(
                this, document, document == QLatin1String("room-palettes") ? tr("Room palettes")
                                                                           : tr("Ending fade"));
        }
        if (kind == QLatin1String("playtest")) {
            return std::make_unique<SolomonPlaytestModel>(this);
        }
    } catch (const std::exception& e) {
        report(sdk::Severity::Error, kind, QString::fromUtf8(e.what()));
    }
    return nullptr;
}

// ---------------------------------------------------------------- editing

const SolomonPainter& SolomonSession::painter() const {
    if (!painter_ || painterRevision_ != revision_) {
        painter_ = std::make_unique<SolomonPainter>(&state_);
        painterRevision_ = revision_;
    }
    return *painter_;
}

const QVector<AnimationFamily>& SolomonSession::animationFamilies() const {
    if (familiesRevision_ != revision_) {
        families_ = SolomonAnimationModel::families(state_.animations);
        familiesRevision_ = revision_;
    }
    return families_;
}

void SolomonSession::setState(const SolomonState& state, quint64 revision) {
    // Only the kinds whose documents changed list again.
    QStringList changed;
    if (!(state.rooms == state_.rooms)) changed << QStringLiteral("rooms");
    if (state.animations != state_.animations) changed << QStringLiteral("animations");
    if (state.presentation != state_.presentation) changed << QStringLiteral("screens") << QStringLiteral("demo");
    if (!(state.text == state_.text)) {
        changed << QStringLiteral("screens") << QStringLiteral("text") << QStringLiteral("palettes");
    }
    if (state.audio != state_.audio) changed << QStringLiteral("sound");
    if (state.art != state_.art || !(state.graphics == state_.graphics)) {
        changed << QStringLiteral("graphics") << QStringLiteral("palettes") << QStringLiteral("rooms");
    }
    changed.removeDuplicates();
    state_ = state;
    revision_ = revision;
    for (const QString& kind : changed) documents_.remove(kind);
    emit contentChanged();
    for (const QString& kind : changed) emit documentsChanged(kind);
    emit diagnosticsChanged();
}

bool SolomonSession::edit(const QString& text, const std::function<void(SolomonState&)>& change, QString* error) {
    SolomonState next = state_;
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
    undoStack()->push(new sdk::SnapshotCommand<SolomonState>(
        [this](const SolomonState& s, quint64 revision) { setState(s, revision); }, state_, revision_, next,
        nextRevision_++, text));
    return true;
}

bool SolomonSession::editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change,
                                QString* error) {
    return edit(text, [change](SolomonState& s) { change(s.graphics); }, error);
}

bool SolomonSession::save(QString* error) {
    try {
        if (state_.rooms != saved_.rooms) saveJson(roomsPath_, state_.rooms.root());
        if (state_.art != saved_.art) saveJson(artPath_, state_.art);
        if (state_.animations != saved_.animations) saveJson(animationsPath_, state_.animations);
        if (state_.presentation != saved_.presentation && !presentationPath_.isEmpty()) {
            saveJson(presentationPath_, state_.presentation);
        }
        if (state_.audio != saved_.audio && !audioPath_.isEmpty()) saveJson(audioPath_, state_.audio);
        if (!(state_.text == saved_.text) && !textPath_.isEmpty()) saveJson(textPath_, state_.text.root());
        if (state_.graphics != saved_.graphics) state_.graphics.save(graphicsPath_);
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return false;
    }
    saved_ = state_;
    undoStack()->setClean();
    documents_.clear();
    for (const sdk::Kind& k : kinds()) {
        emit documentsChanged(k.id);
    }
    return true;
}

QVector<sdk::Diagnostic> SolomonSession::diagnostics() const {
    QVector<sdk::Diagnostic> result;
    const auto check = [&](const QString& stream, int used, int capacity) {
        if (used <= capacity) return;
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("rooms");
        d.kind = QStringLiteral("rooms");
        d.location = tr("All rooms");
        d.message = tr("The %1 streams of all rooms need %2 of %3 bytes.").arg(stream).arg(used).arg(capacity);
        result << d;
    };
    const QString textOverflow = state_.text.isEmpty() ? QString() : state_.text.overflow();
    if (!textOverflow.isEmpty()) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("text");
        d.kind = QStringLiteral("text");
        d.location = QStringLiteral("Text");
        d.message = textOverflow;
        result << d;
    }
    const QString overflow = state_.rooms.overflow();
    if (!overflow.isEmpty()) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("rooms");
        d.kind = QStringLiteral("rooms");
        d.location = tr("All rooms");
        d.message = overflow;
        result << d;
    }
    Q_UNUSED(check);
    return result;
}

void SolomonSession::setStartRoom(int room) {
    room = std::clamp(room, 1, std::max(1, state_.rooms.roomCount()));
    if (room != startRoom_) {
        startRoom_ = room;
        emit startChanged();
        emit selectedStartChanged();
    }
}

void SolomonSession::playDemo(bool record) {
    if (!recordingActive_) launchDemo(record);
}

void SolomonSession::stopRecording() {
    if (recordingActive_ && host()) host()->stopEmulator();
}

void SolomonSession::attachDemoEditor(bool attach) {
    const bool before = demoStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (demoStart() != before) {
        emit startChanged();
        emit selectedStartChanged();
    }
}

QString SolomonSession::playSelectedLabel() const {
    if (recordingActive_) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

QString SolomonSession::selectedStart() const {
    if (demoStart()) return tr("Attract demo");
    return roomLabel(startRoom_);
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> SolomonSession::previewPalettes(const QString&, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const QVector<int> values = state_.graphics.tableValues(QStringLiteral("room-palettes"));
    for (int p = 0; p < 4; ++p) {
        const int at = (sprites ? 16 : 0) + p * 4;
        if (at + 4 <= values.size()) {
            result << nes::PreviewPalette{
                tr("Room %1 %2").arg(sprites ? QStringLiteral("SPR") : QStringLiteral("BG")).arg(p),
                values.mid(at, 4)};
        }
    }
    return result;
}

QStringList SolomonSession::paletteRowLabels(const QString& table) const {
    QStringList labels;
    const int rows = state_.graphics.tableValues(table).size() / 4;
    for (int r = 0; r < rows; ++r) {
        labels << (table == QLatin1String("room-palettes")
                       ? (r < 4 ? tr("BG %1").arg(r) : tr("SPR %1").arg(r - 4))
                       : tr("Step %1").arg(r));
    }
    return labels;
}

QVector<sdk::PaletteSample> SolomonSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    if (table != QLatin1String("room-palettes")) {
        return result;
    }
    sdk::PaletteSample sample;
    sample.label = roomLabel(startRoom_);
    sample.caption = tr("%1, as the game draws it; colour 1 of each row is the world's own").arg(
        roomLabel(startRoom_));
    sample.image = painter().room(startRoom_ - 1);
    result << sample;
    return result;
}

// ---------------------------------------------------------------- build and play

void SolomonSession::report(sdk::Severity severity, const QString& source, const QString& message) {
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

void SolomonSession::runBuild(std::function<void(bool)> next) {
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
        documents_.remove(QStringLiteral("playtest"));
        report(sdk::Severity::Info, QStringLiteral("build"),
               tr("Built %1 (%2 bytes).")
                   .arg(artifact.value(QStringLiteral("path")).toString())
                   .arg(artifact.value(QStringLiteral("size")).toInt()));
        emit documentsChanged(QStringLiteral("playtest"));
        next(true);
    });
}

void SolomonSession::upgradeText() {
    sdk::Host* h = host();
    if (!h || h->isBusy()) return;
    QString error;
    sdk::ProcessSpec spec =
        h->kitCommand({QStringLiteral("studio"), QStringLiteral("solomon-upgrade"), project_.path()}, project_.directory(), &error);
    if (spec.program.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("text"), error);
        return;
    }
    spec.label = tr("Add screen text");
    h->runProcess(spec, [this](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(sdk::Severity::Error, QStringLiteral("text"),
                       tr("Adding the screen text failed: %1").arg(failureText(result)));
            }
            return;
        }
        QString loadError;
        const sdk::ProjectFile reloaded = sdk::ProjectFile::load(project_.path(), &loadError);
        const QString text = reloaded.path().isEmpty() ? QString() : tabDocument(reloaded, QStringLiteral("solomon.text@1"));
        if (text.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("text"), tr("The kit added no text document. %1").arg(loadError));
            return;
        }
        project_ = reloaded;
        textPath_ = project_.resolve(text);
        try {
            const SolomonText loaded(loadJson(textPath_));
            // Part of the project from now on, not an edit to undo.
            state_.text = loaded;
            saved_.text = loaded;
            documents_.clear();
        } catch (const std::exception& e) {
            report(sdk::Severity::Error, QStringLiteral("text"), QString::fromUtf8(e.what()));
            return;
        }
        report(sdk::Severity::Info, QStringLiteral("text"),
               tr("Added documents/text.json: the screen text and static palettes."));
        emit contentChanged();
        for (const QString& kind : {QStringLiteral("text"), QStringLiteral("screens"), QStringLiteral("palettes")}) {
            emit documentsChanged(kind);
        }
    });
}

void SolomonSession::build() {
    runBuild([](bool) {});
}

void SolomonSession::play() {
    launch(true);
}

void SolomonSession::playSelected() {
    if (recordingActive_) {
        if (host()) host()->stopEmulator();
        return;
    }
    if (demoStart()) launchDemo(false);
    else launch(false);
}

void SolomonSession::launchDemo(bool record) {
    runBuild([this, record](bool ok) {
        sdk::Host* h = host();
        if (!ok || !h) return;
        const QString mame = h->emulatorPath(QStringLiteral("mame"));
        if (mame.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"),
                   tr("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
            return;
        }
        QStringList arguments{QStringLiteral("studio"), QStringLiteral("solomon-demo-plan"), project_.path(),
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
        plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
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
            for (int i = 0; i < 64; ++i) game.palette << nes::masterColor(i);
            // The Playtest view covers the boot until the demo room loads.
            game.readyFile = project_.resolve(QStringLiteral("build/demo-ready.txt"));
            game.environment.insert(QStringLiteral("SOLOMON_DEMO_READY"), game.readyFile);
            if (record) {
                recordingActive_ = true;
                emit selectedStartChanged();
                report(sdk::Severity::Info, QStringLiteral("demo"),
                       tr("Recording: play the demo room with the controller; press Start or Stop to finish."));
            }
            const QString title = record ? tr("Solomon's Key · recording the demo")
                                         : tr("Solomon's Key · attract demo");
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

void SolomonSession::applyRecording(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    const int capacity = SolomonDemoModel::demoCapacity(state_.rooms.revision());
    const SolomonDemoModel::Recording recording =
        SolomonDemoModel::stepsFromRecording(QString::fromUtf8(file.readAll()), capacity);
    if (!recording.pressed) {
        report(sdk::Severity::Warning, QStringLiteral("demo"),
               tr("No buttons were pressed while recording; the demo stays as it was."));
        return;
    }
    const QJsonArray steps = recording.steps;
    QString error;
    if (!edit(tr("Record the attract demo"),
              [steps](SolomonState& s) { s.presentation.insert(QStringLiteral("demo_steps"), steps); }, &error)) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    QString message = tr("Recorded %1 s of input into the demo's %2 steps.")
                          .arg(recording.keptFrames / 60.0, 0, 'f', 1)
                          .arg(steps.size());
    if (recording.droppedFrames > 0) {
        message += tr(" The demo holds %1 steps; the last %2 s did not fit.")
                       .arg(capacity)
                       .arg(recording.droppedFrames / 60.0, 0, 'f', 1);
    }
    report(recording.droppedFrames > 0 ? sdk::Severity::Warning : sdk::Severity::Info, QStringLiteral("demo"), message);
}

void SolomonSession::launch(bool fromStart) {
    const int room = startRoom_;
    runBuild([this, fromStart, room](bool ok) {
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
            // The kit numbers rooms from 0.
            arguments = QStringList{QStringLiteral("studio"), QStringLiteral("solomon-point-playtest-plan"),
                                    project_.path(), QStringLiteral("--mame"), mame, QStringLiteral("--room"),
                                    QString::number(room - 1)};
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        const QString title = fromStart ? tr("Solomon's Key · power-on")
                                        : tr("Solomon's Key · %1").arg(roomLabel(room));
        plan.label = fromStart ? tr("Plan playtest from power-on")
                               : tr("Plan playtest of %1").arg(roomLabel(room));
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
            for (int i = 0; i < 64; ++i) {
                game.palette << nes::masterColor(i);
            }
            if (!fromStart) {
                // The Playtest view covers the boot until the room is up.
                game.readyFile = project_.resolve(QStringLiteral("build/playtest-ready.txt"));
                game.environment.insert(QStringLiteral("SOLOMON_PLAYTEST_RESULT"), game.readyFile);
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

}  // namespace rl::solomon
