#include "castle/plugin/castle_session.hpp"

#include "castle/plugin/room_model.hpp"
#include "castle/plugin/table_models.hpp"
#include "nes/palette.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoStack>

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
    return QStringLiteral("exit code %1").arg(result.exitCode);
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

QString first(const nes::Bundle& bundle, const char* codec) {
    const QStringList ids = bundle.resourceIds(QString::fromLatin1(codec));
    return ids.isEmpty() ? QString() : ids.first();
}

}  // namespace

QString roomLabel(int room) {
    return QStringLiteral("Room %1").arg(room);
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
        if (session->art_.chr.isEmpty() || session->art_.roomPalette.isEmpty() ||
            g.tableValues(session->art_.roomPalette).size() < 32) {
            throw CastleError("The graphics bundle lacks the room patterns or palette.");
        }
        session->painter();
    } catch (const std::exception& e) {
        if (error) {
            *error = QStringLiteral("Cannot open the Castle project: %1").arg(QString::fromUtf8(e.what()));
        }
        return nullptr;
    }
    return session;
}

sdk::Profile CastleSession::profile() const {
    sdk::Profile profile;
    const bool usa = state_.levels.revision().contains(QLatin1String("usa"));
    profile.name = usa ? QStringLiteral("Castlequest") : QStringLiteral("Castle Excellent");
    profile.badge = QStringLiteral("NES");
    profile.platform = QStringLiteral("NES");
    profile.revision = usa ? QStringLiteral("U") : QStringLiteral("J");
    profile.cpu = QStringLiteral("6502");
    profile.chip = QStringLiteral("2A03");
    profile.codec = QStringLiteral("castle.prefix-rooms@1");
    return profile;
}

QVector<sdk::Kind> CastleSession::kinds() const {
    return {kind("rooms", "Rooms", "levels", sdk::EditorType::TileMap),
            kind("graphics", "Tiles", "tiles", sdk::EditorType::Tile),
            kind("palettes", "Palettes", "palettes", sdk::EditorType::Palette),
            kind("text", "Text", "text", sdk::EditorType::Table),
            kind("sfx", "Sound FX", "sound", sdk::EditorType::Table),
            kind("playtest", "Playtest", "playtest", sdk::EditorType::Emulator)};
}

QVector<sdk::DocumentGroup> CastleSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    if (kind == QLatin1String("rooms")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Castle");
        for (int r = 0; r < state_.levels.roomCount(); ++r) {
            const QVector<CastleObject> objects = state_.levels.objects(r);
            const bool dirty = state_.levels.cells(r) != saved_.levels.cells(r) || objects != saved_.levels.objects(r);
            group.documents << entry(QString::number(r + 1), roomLabel(state_.levels.roomNumber(r)),
                                     objects.isEmpty() ? QString() : QStringLiteral("%1 obj").arg(objects.size()),
                                     dirty);
        }
        groups << group;
    } else if (kind == QLatin1String("graphics")) {
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.chr-2bpp.v1"))) {
            sdk::DocumentGroup group;
            const int count = state_.graphics.tileCount(id);
            group.title = id == art_.chr ? QStringLiteral("%1 · rooms").arg(id) : id;
            for (int half = 0; half * 256 < count; ++half) {
                bool dirty = false;
                for (int t = half * 256; t < std::min(count, half * 256 + 256) && !dirty; ++t) {
                    dirty = state_.graphics.tilePixels(id, t) != saved_.graphics.tilePixels(id, t);
                }
                group.documents << entry(QStringLiteral("%1/%2").arg(id).arg(half),
                                         half == 0 ? QStringLiteral("Pattern table 0") : QStringLiteral("Pattern table 1"),
                                         id == art_.chr ? (half == 0 ? QStringLiteral("sprites")
                                                                     : QStringLiteral("room cells"))
                                                        : QStringLiteral("256 tiles"),
                                         dirty);
            }
            groups << group;
        }
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Palettes");
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1"))) {
            const bool dirty = state_.graphics.tableValues(id) != saved_.graphics.tableValues(id);
            group.documents << entry(id,
                                     id == QLatin1String("room-palettes")     ? QStringLiteral("Rooms")
                                     : id == QLatin1String("screen-palettes") ? QStringLiteral("Screens")
                                                                              : id,
                                     QStringLiteral("%1 colours").arg(state_.graphics.tableValues(id).size()), dirty);
        }
        groups << group;
    } else if (kind == QLatin1String("text")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Text");
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle.sub-command-text.v1"))) {
            group.documents << entry(id, QStringLiteral("Menu text"), QString(),
                                     state_.graphics.resource(id) != saved_.graphics.resource(id));
        }
        groups << group;
    } else if (kind == QLatin1String("sfx")) {
        sdk::DocumentGroup group;
        group.title = QStringLiteral("Sound effects");
        for (const QString& id : state_.graphics.resourceIds(QStringLiteral("castle.sfx-parameters.v1"))) {
            group.documents << entry(id, QStringLiteral("Parameters"), QString(),
                                     state_.graphics.resource(id) != saved_.graphics.resource(id));
        }
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

std::unique_ptr<sdk::EditorModel> CastleSession::openEditor(const QString& kind, const QString& document) {
    try {
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
                    QStringLiteral("%1 · table %2").arg(id).arg(half), half == 0);
            }
        }
        if (kind == QLatin1String("palettes") &&
            state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1")).contains(document)) {
            return std::make_unique<nes::PaletteTableModel>(
                this, document, document == QLatin1String("room-palettes") ? QStringLiteral("Room palettes")
                                                                           : QStringLiteral("Screen palettes"));
        }
        if (kind == QLatin1String("text") &&
            state_.graphics.resourceIds(QStringLiteral("castle.sub-command-text.v1")).contains(document)) {
            return std::make_unique<CastleTextModel>(this, document);
        }
        if (kind == QLatin1String("sfx") &&
            state_.graphics.resourceIds(QStringLiteral("castle.sfx-parameters.v1")).contains(document)) {
            return std::make_unique<CastleSfxModel>(this, document);
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
    state_ = state;
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
    next.pixel = QPoint(std::clamp(next.pixel.x(), 0, 319), std::clamp(next.pixel.y(), 0, 159));
    if (next.room == start_.room && next.pixel == start_.pixel) {
        return;
    }
    start_ = next;
    emit startChanged();
    emit selectedStartChanged();
}

QString CastleSession::selectedStart() const {
    return QStringLiteral("%1 · %2, %3").arg(roomLabel(start_.room)).arg(start_.pixel.x()).arg(start_.pixel.y());
}

// ---------------------------------------------------------------- palettes

QVector<nes::PreviewPalette> CastleSession::previewPalettes(const QString&, bool sprites) const {
    QVector<nes::PreviewPalette> result;
    const QVector<int> room = state_.graphics.tableValues(art_.roomPalette);
    for (int p = 0; p < 4; ++p) {
        const int at = (sprites ? 16 : 0) + p * 4;
        if (at + 4 <= room.size()) {
            result << nes::PreviewPalette{QStringLiteral("Room %1 %2").arg(sprites ? QStringLiteral("SPR") : QStringLiteral("BG")).arg(p),
                                          room.mid(at, 4)};
        }
    }
    for (const QString& id : state_.graphics.resourceIds(QStringLiteral("nes.palette-table.v1"))) {
        if (id == art_.roomPalette) continue;
        const QVector<int> values = state_.graphics.tableValues(id);
        for (int at = 0; at + 4 <= values.size(); at += 4) {
            result << nes::PreviewPalette{QStringLiteral("Screen %1").arg(at / 4), values.mid(at, 4)};
        }
    }
    return result;
}

QStringList CastleSession::paletteRowLabels(const QString& table) const {
    QStringList labels;
    const int rows = state_.graphics.tableValues(table).size() / 4;
    for (int r = 0; r < rows; ++r) {
        const int set = r / 8;
        const QString kind = r % 8 < 4 ? QStringLiteral("BG %1").arg(r % 4) : QStringLiteral("SPR %1").arg(r % 4);
        labels << (rows > 8 ? QStringLiteral("Set %1 · %2").arg(set).arg(kind) : kind);
    }
    return labels;
}

QVector<sdk::PaletteSample> CastleSession::paletteSamples(const QString& table) const {
    QVector<sdk::PaletteSample> result;
    if (table != art_.roomPalette || state_.levels.roomCount() == 0) {
        return result;
    }
    sdk::PaletteSample sample;
    sample.label = roomLabel(start_.room);
    sample.caption = QStringLiteral("%1, as the game draws it").arg(roomLabel(start_.room));
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
        lastBuild_ = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
        report(sdk::Severity::Info, QStringLiteral("build"),
               QStringLiteral("Built %1 (%2 bytes).")
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
    launch(false);
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
                   QStringLiteral("MAME was not found. Set RETROLARIUM_MAME to consoles.exe."));
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
                                    QString::number(start.pixel.y())};
        }
        QString error;
        sdk::ProcessSpec plan = h->kitCommand(arguments, project_.directory(), &error);
        if (plan.program.isEmpty()) {
            report(sdk::Severity::Error, QStringLiteral("playtest"), error);
            return;
        }
        const QString name = profile().name;
        const QString title = fromStart ? QStringLiteral("%1 · power-on").arg(name)
                                        : QStringLiteral("%1 · %2").arg(name, roomLabel(start.room));
        plan.label = fromStart ? QStringLiteral("Plan playtest from power-on")
                               : QStringLiteral("Plan playtest of %1").arg(roomLabel(start.room));
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
            sdk::ProcessSpec game = specFromPlan(QJsonDocument::fromJson(result.standardOutput).object());
            if (game.program.isEmpty()) {
                report(sdk::Severity::Error, QStringLiteral("playtest"), QStringLiteral("The kit returned no emulator command."));
                return;
            }
            const std::string model = state_.graphics.paletteModel(art_.roomPalette).toStdString();
            for (int i = 0; i < 64; ++i) {
                game.palette << nes::masterColor(i, model);
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

}  // namespace rl::castle
