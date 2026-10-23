#include "alien_soldier/plugin/session.hpp"

#include "alien_soldier/plugin/animations_model.hpp"
#include "alien_soldier/plugin/demo_model.hpp"
#include "alien_soldier/plugin/levels_model.hpp"
#include "alien_soldier/plugin/metatiles_model.hpp"
#include "alien_soldier/plugin/palettes_model.hpp"
#include "alien_soldier/plugin/playtest_model.hpp"
#include "alien_soldier/plugin/screens_model.hpp"
#include "alien_soldier/plugin/sound_model.hpp"
#include "alien_soldier/plugin/text_model.hpp"
#include "alien_soldier/plugin/tiles_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/snapshot_command.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoStack>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

const QString kSnapshot = QStringLiteral("alien-soldier-source");

sdk::DocumentEntry entry(const QString& id, const QString& label, const QString& meta, bool dirty = false) {
    sdk::DocumentEntry result;
    result.id = id;
    result.label = label;
    result.meta = meta;
    result.dirty = dirty;
    return result;
}

sdk::Kind kind(const char* id, const QString& label, sdk::EditorType editor, const char* icon = nullptr) {
    sdk::Kind result;
    result.id = QString::fromLatin1(id);
    result.label = label;
    result.icon = QString::fromLatin1(icon ? icon : id);
    result.editor = editor;
    return result;
}

QString two(int value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

// Which documents a view's document list marks dirty by.
bool payloadDirty(const QJsonObject& now, const QJsonObject& saved, const QString& list, const QString& label) {
    const QJsonArray a = now.value(list).toArray(), b = saved.value(list).toArray();
    const int index = indexOfLabel(a, label);
    return index >= 0 && a.at(index) != b.at(index);
}

}  // namespace

// ---------------------------------------------------------------- opening

std::unique_ptr<AlienSoldierSession> AlienSoldierSession::open(const sdk::ProjectFile& project, QString* error) {
    std::unique_ptr<AlienSoldierSession> session(new AlienSoldierSession);
    session->project_ = project;
    try {
        session->snapshot_ = project.resolve(kSnapshot);
        session->documents_ = Documents::load(session->snapshot_);
        const QJsonObject record =
            loadJson(QDir(session->snapshot_).filePath(QStringLiteral("retrolarium-source-snapshot.json")));
        session->revision_ = record.value(QStringLiteral("revision")).toString();
    } catch (const std::exception& e) {
        if (error) *error = tr("Cannot open the Alien Soldier project: %1").arg(QString::fromUtf8(e.what()));
        return nullptr;
    }
    return session;
}

sdk::Profile AlienSoldierSession::profile() const {
    sdk::Profile profile;
    profile.name = QStringLiteral("Alien Soldier");
    profile.badge = QStringLiteral("MD");
    profile.platform = tr("Mega Drive");
    profile.revision = revision_.toUpper();
    profile.cpu = QStringLiteral("68000");
    profile.chip = tr("YM2612 + PSG");
    profile.codec = QStringLiteral("source");
    return profile;
}

QVector<sdk::Kind> AlienSoldierSession::kinds() const {
    return {
        kind("levels", tr("Levels"), sdk::EditorType::TileMap),
        kind("tiles", tr("Tiles"), sdk::EditorType::Tile),
        kind("metatiles", tr("Metatiles"), sdk::EditorType::TileMap, "tileset"),
        kind("palettes", tr("Palettes"), sdk::EditorType::Palette),
        kind("sprites", tr("Animations"), sdk::EditorType::Composition),
        kind("screens", tr("Screens"), sdk::EditorType::TileMap, "tileset"),
        kind("text", tr("Text"), sdk::EditorType::Table),
        kind("sound", tr("Sound"), sdk::EditorType::Sequencer),
        kind("demo", tr("Demo"), sdk::EditorType::Table, "play"),
        kind("playtest", tr("Playtest"), sdk::EditorType::Emulator),
    };
}

int AlienSoldierSession::stageCount() const {
    return doc(Doc::Stages).value(QStringLiteral("scenes")).toArray().size();
}

QString AlienSoldierSession::stageLabel(int index) const {
    const QString name = sdk::gameName("stages", index, QString());
    return name.isEmpty() ? tr("Stage slot %1").arg(two(index + 1)) : tr("Stage %1 · %2").arg(two(index + 1), name);
}

// ---------------------------------------------------------------- documents

QVector<sdk::DocumentGroup> AlienSoldierSession::documents(const QString& kind) const {
    QVector<sdk::DocumentGroup> groups;
    const auto group = [](const QString& title) {
        sdk::DocumentGroup result;
        result.title = title;
        return result;
    };
    if (kind == QLatin1String("levels") || kind == QLatin1String("metatiles")) {
        sdk::DocumentGroup stages = group(tr("Stages"));
        const QJsonArray scenes = doc(Doc::Stages).value(QStringLiteral("scenes")).toArray();
        const bool levelsDirty = documents_.tracked(Doc::Levels).isDirty();
        const bool mapsDirty = documents_.tracked(Doc::Maps).isDirty();
        for (int index = 0; index < scenes.size(); ++index) {
            const QJsonObject scene = scenes.at(index).toObject();
            const bool drawable = !scene.value(QStringLiteral("planes")).toArray().at(0).isNull() ||
                                  !scene.value(QStringLiteral("planes")).toArray().at(1).isNull();
            stages.documents.append(entry(QStringLiteral("stage-") + two(index + 1), stageLabel(index),
                                          drawable ? QString() : tr("no map"),
                                          kind == QLatin1String("levels") ? levelsDirty : mapsDirty));
        }
        groups.append(stages);
    } else if (kind == QLatin1String("tiles")) {
        sdk::DocumentGroup compressed = group(tr("Compressed"));
        for (const QJsonValue& value : doc(Doc::Compressed).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString label = payload.value(QStringLiteral("label")).toString();
            compressed.documents.append(entry(
                QStringLiteral("c:") + label, label,
                QStringLiteral("%1").arg(payload.value(QStringLiteral("tiles")).toArray().size()),
                payloadDirty(doc(Doc::Compressed), savedDoc(Doc::Compressed), QStringLiteral("payloads"), label)));
        }
        sdk::DocumentGroup plain = group(tr("Uncompressed"));
        for (const QJsonValue& value : doc(Doc::Tiles).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString label = payload.value(QStringLiteral("label")).toString();
            int count = 0;
            for (const QJsonValue& block : payload.value(QStringLiteral("blocks")).toArray()) {
                count += block.toObject().value(QStringLiteral("tiles")).toArray().size();
            }
            plain.documents.append(
                entry(QStringLiteral("u:") + label, label, QString::number(count),
                      payloadDirty(doc(Doc::Tiles), savedDoc(Doc::Tiles), QStringLiteral("payloads"), label)));
        }
        groups << compressed << plain;
    } else if (kind == QLatin1String("palettes")) {
        sdk::DocumentGroup blocks = group(tr("Palette commands"));
        for (const QJsonValue& value : doc(Doc::Palettes).value(QStringLiteral("blocks")).toArray()) {
            const QJsonObject block = value.toObject();
            const QString label = block.value(QStringLiteral("label")).toString();
            blocks.documents.append(
                entry(label, label, QString::number(block.value(QStringLiteral("commands")).toArray().size()),
                      payloadDirty(doc(Doc::Palettes), savedDoc(Doc::Palettes), QStringLiteral("blocks"), label)));
        }
        groups.append(blocks);
    } else if (kind == QLatin1String("sprites")) {
        sdk::DocumentGroup scripts = group(tr("Animations"));
        for (const QJsonValue& value : doc(Doc::Animations).value(QStringLiteral("scripts")).toArray()) {
            const QJsonObject script = value.toObject();
            const QString label = script.value(QStringLiteral("label")).toString();
            scripts.documents.append(
                entry(label, label, QString::number(script.value(QStringLiteral("entries")).toArray().size()),
                      payloadDirty(doc(Doc::Animations), savedDoc(Doc::Animations), QStringLiteral("scripts"), label) ||
                          documents_.tracked(Doc::Mappings).isDirty()));
        }
        groups.append(scripts);
    } else if (kind == QLatin1String("screens")) {
        sdk::DocumentGroup screens = group(tr("Screens"));
        for (const QJsonValue& value : doc(Doc::Screens).value(QStringLiteral("screens")).toArray()) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString();
            screens.documents.append(
                entry(id, ScreensModel::screenTitle(id), QString(), documents_.tracked(Doc::Screens).isDirty()));
        }
        groups.append(screens);
    } else if (kind == QLatin1String("text")) {
        QMap<QString, sdk::DocumentGroup> byGroup;
        QStringList order;
        for (const QJsonValue& value : doc(Doc::Text).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString name = payload.value(QStringLiteral("group")).toString();
            if (!byGroup.contains(name)) {
                byGroup.insert(name, group(TextModel::groupTitle(name)));
                order << name;
            }
            const QString label = payload.value(QStringLiteral("label")).toString();
            byGroup[name].documents.append(
                entry(label, label, QString::number(payload.value(QStringLiteral("records")).toArray().size()),
                      payloadDirty(doc(Doc::Text), savedDoc(Doc::Text), QStringLiteral("payloads"), label)));
        }
        for (const QString& name : order) groups.append(byGroup.value(name));
    } else if (kind == QLatin1String("sound")) {
        sdk::DocumentGroup music = group(tr("Music"));
        sdk::DocumentGroup effects = group(tr("Sound effects"));
        for (const QJsonValue& value : doc(Doc::Sound).value(QStringLiteral("payloads")).toArray()) {
            const QJsonObject payload = value.toObject();
            const QString label = payload.value(QStringLiteral("label")).toString();
            QStringList ids;
            for (const QJsonValue& request : payload.value(QStringLiteral("requests")).toArray()) {
                ids << QStringLiteral("$%1").arg(request.toInt(), 2, 16, QLatin1Char('0')).toUpper();
            }
            const bool bgm = payload.value(QStringLiteral("kind")).toString() == QLatin1String("bgm");
            (bgm ? music : effects)
                .documents.append(
                    entry(label, label, ids.join(QLatin1Char(' ')),
                          payloadDirty(doc(Doc::Sound), savedDoc(Doc::Sound), QStringLiteral("payloads"), label)));
        }
        groups << music << effects;
    } else if (kind == QLatin1String("demo")) {
        sdk::DocumentGroup demos = group(tr("Attract demos"));
        const QJsonArray list = doc(Doc::Demos).value(QStringLiteral("demos")).toArray();
        for (int index = 0; index < list.size(); ++index) {
            const int stage = list.at(index).toObject().value(QStringLiteral("stage")).toInt() / 2;
            demos.documents.append(
                entry(QStringLiteral("demo-%1").arg(index + 1), tr("Demo %1").arg(index + 1), stageLabel(stage),
                      list.at(index) != savedDoc(Doc::Demos).value(QStringLiteral("demos")).toArray().at(index)));
        }
        groups.append(demos);
    } else if (kind == QLatin1String("playtest")) {
        sdk::DocumentGroup session = group(tr("Session"));
        session.documents.append(entry(QStringLiteral("playtest"), tr("Build and run"), stageLabel(playStage_)));
        groups.append(session);
    }
    return groups;
}

std::unique_ptr<sdk::EditorModel> AlienSoldierSession::openEditor(const QString& kind, const QString& document) {
    const auto stageOf = [&document]() {
        bool ok = false;
        const int number = document.mid(6).toInt(&ok);
        return ok ? number - 1 : -1;
    };
    if (kind == QLatin1String("levels")) {
        const int stage = stageOf();
        if (stage < 0 || stage >= stageCount()) return nullptr;
        if (stage < passwordStages()) setPlayStage(stage);
        return std::make_unique<LevelsModel>(this, stage);
    }
    if (kind == QLatin1String("metatiles")) {
        const int stage = stageOf();
        return stage < 0 || stage >= stageCount() ? nullptr : std::make_unique<MetatilesModel>(this, stage);
    }
    if (kind == QLatin1String("tiles")) {
        if (document.startsWith(QLatin1String("c:")))
            return std::make_unique<TilesModel>(this, Doc::Compressed, document.mid(2));
        if (document.startsWith(QLatin1String("u:")))
            return std::make_unique<TilesModel>(this, Doc::Tiles, document.mid(2));
        return nullptr;
    }
    if (kind == QLatin1String("palettes")) return std::make_unique<PalettesModel>(this, document);
    if (kind == QLatin1String("sprites")) return std::make_unique<AnimationsModel>(this, document);
    if (kind == QLatin1String("screens")) return std::make_unique<ScreensModel>(this, document);
    if (kind == QLatin1String("text")) return std::make_unique<TextModel>(this, document);
    if (kind == QLatin1String("sound")) return std::make_unique<SoundModel>(this, document);
    if (kind == QLatin1String("demo")) {
        const int demo = document.mid(5).toInt() - 1;
        return demo < 0 || demo > 3 ? nullptr : std::make_unique<DemoModel>(this, demo);
    }
    if (kind == QLatin1String("playtest")) return std::make_unique<PlaytestModel>(this);
    return nullptr;
}

// ---------------------------------------------------------------- editing

void AlienSoldierSession::setDocument(Doc which, const QJsonObject& value, quint64 revision) {
    Tracked& tracked = documents_.tracked(which);
    tracked.doc = value;
    tracked.revision = revision;
    changed(which);
}

void AlienSoldierSession::changed(Doc which) {
    switch (which) {
        case Doc::Maps:
        case Doc::Tiles:
        case Doc::Compressed:
        case Doc::Palettes:
        case Doc::Levels:
        case Doc::Screens:
            stages_.clear();
            screens_.clear();
            break;
        default:
            break;
    }
    emit documentChanged(which);
    emit contentChanged();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
}

bool AlienSoldierSession::edit(Doc which, const QString& text, const std::function<void(QJsonObject&)>& change,
                               QString* error) {
    Tracked& tracked = documents_.tracked(which);
    QJsonObject after = tracked.doc;
    try {
        change(after);
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    if (after == tracked.doc) return true;
    const quint64 before = tracked.revision;
    const quint64 next = tracked.nextRevision++;
    undoStack()->push(new sdk::SnapshotCommand<QJsonObject>(
        [this, which](const QJsonObject& value, quint64 revision) { setDocument(which, value, revision); }, tracked.doc,
        before, after, next, text));
    return true;
}

void AlienSoldierSession::touch(Doc which) { changed(which); }

void AlienSoldierSession::commit(Doc which, const QJsonObject& before, quint64 beforeRevision, const QString& text) {
    Tracked& tracked = documents_.tracked(which);
    if (tracked.doc == before) return;
    const quint64 next = tracked.nextRevision++;
    // The stroke already shows; the first redo of a pushed command applies it again.
    undoStack()->push(new sdk::SnapshotCommand<QJsonObject>(
        [this, which](const QJsonObject& value, quint64 revision) { setDocument(which, value, revision); }, before,
        beforeRevision, tracked.doc, next, text));
}

bool AlienSoldierSession::save(QString* error) {
    bool ok = true;
    for (int index = 0; index < kDocCount; ++index) {
        const Doc which = static_cast<Doc>(index);
        Tracked& tracked = documents_.tracked(which);
        if (!isEditable(which) || !tracked.isDirty()) continue;
        try {
            saveJson(tracked.path, tracked.doc);
            tracked.saved = tracked.doc;
            tracked.savedRevision = tracked.revision;
        } catch (const std::exception& e) {
            if (ok && error)
                *error = tr("Cannot save %1: %2").arg(QFileInfo(tracked.path).fileName(), QString::fromUtf8(e.what()));
            ok = false;
        }
    }
    if (ok) undoStack()->setClean();
    for (const sdk::Kind& k : kinds()) emit documentsChanged(k.id);
    return ok;
}

bool AlienSoldierSession::canRevert(const QString& kind, const QString& document) const {
    Q_UNUSED(document);
    return kind != QLatin1String("playtest");
}

bool AlienSoldierSession::revert(const QString& kind, const QString& document, QString* error) {
    // A view reverts the documents it edits, whole.
    QVector<Doc> docs;
    if (kind == QLatin1String("levels")) docs = {Doc::Levels};
    else if (kind == QLatin1String("metatiles")) docs = {Doc::Maps};
    else if (kind == QLatin1String("tiles"))
        docs = {document.startsWith(QLatin1String("u:")) ? Doc::Tiles : Doc::Compressed};
    else if (kind == QLatin1String("palettes")) docs = {Doc::Palettes};
    else if (kind == QLatin1String("sprites")) docs = {Doc::Animations, Doc::Mappings};
    else if (kind == QLatin1String("screens")) docs = {Doc::Screens};
    else if (kind == QLatin1String("text")) docs = {Doc::Text};
    else if (kind == QLatin1String("sound")) docs = {Doc::Sound, Doc::Envelopes, Doc::Pcm};
    else if (kind == QLatin1String("demo")) docs = {Doc::Demos};
    for (Doc which : docs) {
        const QJsonObject original = documents_.tracked(which).original;
        if (!edit(which, tr("Revert %1").arg(kind), [&original](QJsonObject& value) { value = original; }, error))
            return false;
    }
    return true;
}

const Scene& AlienSoldierSession::stage(int index) const {
    auto found = stages_.find(index);
    if (found == stages_.end()) found = stages_.insert(index, stageScene(documents_, index));
    return found.value();
}

const Scene& AlienSoldierSession::screen(int index) const {
    auto found = screens_.find(index);
    if (found == screens_.end()) found = screens_.insert(index, screenScene(documents_, index));
    return found.value();
}

// ---------------------------------------------------------------- playing

void AlienSoldierSession::setPlayStage(int stage) {
    stage = std::clamp(stage, 0, passwordStages() - 1);
    if (stage == playStage_) return;
    playStage_ = stage;
    emit playStageChanged();
    emit selectedStartChanged();
    emit documentsChanged(QStringLiteral("playtest"));
}

QString AlienSoldierSession::selectedStart() const {
    return demoStart() ? tr("Demo %1").arg(demo_ + 1) : stageLabel(playStage_);
}

QString AlienSoldierSession::playSelectedLabel() const {
    if (isRecording()) return tr("Stop recording");
    return demoStart() ? tr("Play demo") : QString();
}

void AlienSoldierSession::build() {
    runBuild([](bool) {}, true);
}

void AlienSoldierSession::play() { launch(-1); }

void AlienSoldierSession::playSelected() {
    if (isRecording()) {
        stopRecording();
    } else if (demoStart()) {
        launchDemo(demo_, false);
    } else {
        launch(playStage_);
    }
}

void AlienSoldierSession::launch(int stage) {
    EmulatorPlan plan;
    plan.force = false;
    if (stage >= 0) {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("alien-soldier-playtest-plan"),
                                     project_.path(), QStringLiteral("--stage"), QString::number(stage)};
        plan.label = tr("Plan playtest of %1").arg(stageLabel(stage));
        plan.title = QStringLiteral("Alien Soldier · %1").arg(stageLabel(stage));
        // The playtest script writes one status line once the stage runs.
        plan.readyFile = QStringLiteral("build/playtest-status.txt");
        plan.readyVariable = QStringLiteral("ALIEN_PLAYTEST_RESULT");
    } else {
        plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("launch-plan"), project_.path()};
        plan.label = tr("Plan playtest from power-on");
        plan.title = tr("Alien Soldier · power-on");
    }
    launchPlanned(plan);
}

void AlienSoldierSession::attachDemoEditor(int demo, bool attach) {
    const QString before = selectedStart();
    demoEditors_ = std::max(0, demoEditors_ + (attach ? 1 : -1));
    if (attach) demo_ = demo;
    if (selectedStart() != before) emit selectedStartChanged();
}

void AlienSoldierSession::playDemo(int demo, bool record) {
    if (!isRecording()) launchDemo(demo, record);
}

void AlienSoldierSession::launchDemo(int demo, bool record) {
    EmulatorPlan plan;
    plan.arguments = QStringList{QStringLiteral("studio"), QStringLiteral("alien-soldier-demo-plan"), project_.path(),
                                 QStringLiteral("--demo"), QString::number(demo)};
    plan.label = record ? tr("Plan demo recording") : tr("Plan attract demo");
    plan.title =
        record ? tr("Alien Soldier · recording demo %1").arg(demo + 1) : tr("Alien Soldier · demo %1").arg(demo + 1);
    plan.demo = true;
    plan.force = false;
    plan.readyFile = QStringLiteral("build/demo-ready.txt");
    plan.readyVariable = QStringLiteral("ALIEN_DEMO_READY");
    if (record) {
        plan.recordTo = QStringLiteral("build/demo-recording.txt");
        plan.recordingMessage =
            tr("Recording: the demo starts the stage itself, then the controller plays it; it ends after the demo's "
               "time, on Start or on Stop recording.");
        plan.recorded = [this, demo](const QString& path) { applyRecording(demo, path); };
    }
    launchPlanned(plan);
}

void AlienSoldierSession::applyRecording(int demo, const QString& path) {
    if (!QFileInfo::exists(path)) {
        report(sdk::Severity::Warning, QStringLiteral("demo"), tr("Nothing was recorded: the demo did not start."));
        return;
    }
    // The kit joins the demo's own start (title and weapon setup) with the
    // recorded steps.
    const QString steps = QDir(project_.directory()).filePath(QStringLiteral("build/demo-steps.json"));
    {
        QFile file(steps);
        if (!file.open(QIODevice::WriteOnly)) {
            report(sdk::Severity::Error, QStringLiteral("demo"), tr("Cannot write %1.").arg(steps));
            return;
        }
        const QJsonArray current = doc(Doc::Demos)
                                       .value(QStringLiteral("demos"))
                                       .toArray()
                                       .at(demo)
                                       .toObject()
                                       .value(QStringLiteral("steps"))
                                       .toArray();
        file.write(QJsonDocument(current).toJson(QJsonDocument::Compact));
    }
    QString error;
    sdk::ProcessSpec spec = host()->kitCommand({QStringLiteral("studio"), QStringLiteral("alien-soldier-demo-steps"),
                                                steps, QStringLiteral("--recording"), path},
                                               project_.directory(), &error);
    if (!error.isEmpty()) {
        report(sdk::Severity::Error, QStringLiteral("demo"), error);
        return;
    }
    spec.label = tr("Join the recording to demo %1").arg(demo + 1);
    host()->runProcess(spec, [this, demo](const sdk::ProcessResult& result) {
        if (!result.started || result.exitCode != 0) {
            report(sdk::Severity::Error, QStringLiteral("demo"), sdk::failureText(result));
            return;
        }
        const QJsonArray steps =
            QJsonDocument::fromJson(result.standardOutput).object().value(QStringLiteral("steps")).toArray();
        QString failure;
        const bool ok = edit(
            Doc::Demos, tr("Record demo %1").arg(demo + 1),
            [demo, steps](QJsonObject& value) {
                QJsonArray demos = value.value(QStringLiteral("demos")).toArray();
                QJsonObject item = demos.at(demo).toObject();
                item.insert(QStringLiteral("steps"), steps);
                demos.replace(demo, item);
                value.insert(QStringLiteral("demos"), demos);
            },
            &failure);
        if (!ok) {
            report(sdk::Severity::Error, QStringLiteral("demo"), failure);
            return;
        }
        report(sdk::Severity::Info, QStringLiteral("demo"),
               tr("Recorded demo %1: %2 steps.").arg(demo + 1).arg(steps.size()));
    });
}

}  // namespace rl::alien_soldier
