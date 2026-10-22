#include "solomon/plugin/sound_model.hpp"

#include "nes/editors.hpp"
#include "sdk/plugin.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kTraceFrames = 60 * 40;  // forty seconds of a program
constexpr int kPitchedLimit = 6;       // virtual channels 6 and 7 drive the noise voice
constexpr int kRest = 0x0c;
constexpr int kBase = 24;  // period table entry 0 sounds C2

const char* const kPrograms[26] = {
    "Room audio A / warning return A",
    "Room audio B / warning return B",
    "Room transition reset",
    "Timer warning",
    "Post-game result",
    "Extra life",
    "Create breakable block",
    "Remove breakable block",
    "Enemy drop",
    "Fireball cast",
    "Paired-enemy attack",
    "Pause / PAL resume",
    "Item pickup / enemy reward",
    "NTSC resume",
    "Fairy collected",
    "Ending input prompt",
    "Player head collision",
    "Remove solid block",
    "Room-clear countdown / ending phase",
    "Room entry / ending convergence",
    "Enter door",
    "Collect key",
    "Linked-enemy spawn",
    "Title / new game / room-clear transition",
    "Ending object fall",
    "Ending fade",
};

const char* const kVoices[4] = {"pulse1", "pulse2", "triangle", "noise"};
const char* const kVoiceLabels[4] = {"Pulse 1", "Pulse 2", "Triangle", "Noise"};
const QColor kVoiceColors[4] = {QColor(0x56, 0xb4, 0xe9), QColor(0xe6, 0x9f, 0x00), QColor(0x00, 0x9e, 0x73),
                                QColor(0xcc, 0x79, 0xa7)};

struct Loop {
    int start = 0;
    int remaining = 0;
};

struct Channel {
    int number = 0;
    int pc = 0;
    bool active = true;
    int counter = 1;
    int reload = 0;
    QVector<int> calls;
    QVector<Loop> loops;
};

}  // namespace

QString SolomonSoundModel::programLabel(int program) {
    return QStringLiteral("%1 · %2")
        .arg(program, 2, 10, QLatin1Char('0'))
        .arg(QString::fromLatin1(kPrograms[program - 1]));
}

QVector<QPair<QString, QVector<int>>> SolomonSoundModel::programGroups() {
    return {{tr("Music loops"), {1, 2, 4, 16, 19}},
            {tr("Musical cues"), {5, 6, 15, 20, 22}},
            {tr("Sound effects"), {7, 8, 9, 10, 11, 13, 17, 18, 21, 23, 25, 26}},
            {tr("Engine controls"), {3, 12, 14, 24}}};
}

QHash<int, QVector<SolomonNote>> SolomonSoundModel::trace(const QJsonObject& audio, int program, int frames) {
    QHash<int, QVector<SolomonNote>> result;
    const QJsonArray commands = audio.value(QStringLiteral("commands")).toArray();
    const QJsonArray durations = audio.value(QStringLiteral("durations")).toArray();
    QHash<QString, int> entries;
    for (int i = 0; i < commands.size(); ++i) {
        const QString entry = commands.at(i).toObject().value(QStringLiteral("entry")).toString();
        if (!entry.isEmpty()) entries.insert(entry, i);
    }
    const QJsonObject effect = audio.value(QStringLiteral("effects")).toArray().at(program - 1).toObject();
    QVector<Channel> channels;
    for (const QJsonValue& value : effect.value(QStringLiteral("channels")).toArray()) {
        const QJsonObject c = value.toObject();
        Channel channel;
        channel.number = c.value(QStringLiteral("selector")).toInt() & 7;
        channel.pc = entries.value(c.value(QStringLiteral("stream")).toString(), -1);
        channel.active = channel.pc >= 0;
        channels << channel;
        result.insert(channel.number, {});
    }
    std::sort(channels.begin(), channels.end(), [](const Channel& a, const Channel& b) { return a.number < b.number; });
    // The engine's command loop, as the source reconstruction's preview
    // steps it: a channel reads commands until a note or a stop each time
    // its duration runs out.
    for (int frame = 0; frame < frames; ++frame) {
        bool any = false;
        for (Channel& ch : channels) {
            if (!ch.active) continue;
            any = true;
            ch.counter = (ch.counter - 1) & 0xff;
            if (ch.counter != 0) continue;
            for (int guard = 0; guard < 10000; ++guard) {
                if (ch.pc < 0 || ch.pc >= commands.size()) {
                    ch.active = false;
                    break;
                }
                const QJsonObject command = commands.at(ch.pc).toObject();
                const QString kind = command.value(QStringLiteral("kind")).toString();
                const int value = command.value(QStringLiteral("value")).toInt();
                if (kind == QLatin1String("note")) {
                    QVector<SolomonNote>& list = result[ch.number];
                    if (!list.isEmpty()) list.last().length = frame - list.last().start;
                    list << SolomonNote{frame, std::max(1, ch.reload), ch.pc, value};
                    ch.pc += 1;
                    ch.counter = ch.reload;
                    break;
                }
                ch.pc += 1;
                if (kind == QLatin1String("duration")) {
                    ch.reload = ch.counter = durations.at(value & 0x3f).toInt();
                } else if (kind == QLatin1String("jump")) {
                    ch.pc = entries.value(command.value(QStringLiteral("target")).toString(), -1);
                } else if (kind == QLatin1String("call")) {
                    ch.calls << ch.pc;
                    ch.pc = entries.value(command.value(QStringLiteral("target")).toString(), -1);
                } else if (kind == QLatin1String("return")) {
                    if (ch.calls.isEmpty()) {
                        ch.active = false;
                        break;
                    }
                    ch.pc = ch.calls.takeLast();
                } else if (kind == QLatin1String("begin_loop")) {
                    ch.loops << Loop{ch.pc, value};
                } else if (kind == QLatin1String("end_loop")) {
                    if (ch.loops.isEmpty()) {
                        ch.active = false;
                        break;
                    }
                    Loop& loop = ch.loops.last();
                    loop.remaining = (loop.remaining - 1) & 0xff;
                    if (loop.remaining) ch.pc = loop.start;
                    else ch.loops.removeLast();
                } else if (kind == QLatin1String("stop")) {
                    ch.active = false;
                    QVector<SolomonNote>& list = result[ch.number];
                    if (!list.isEmpty()) list.last().length = frame - list.last().start;
                    break;
                }
            }
        }
        if (!any) break;
    }
    return result;
}

SolomonSoundModel::SolomonSoundModel(SolomonSession* session, int program)
    : SequencerModel(session), session_(session), program_(program) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

const QHash<int, QVector<SolomonNote>>& SolomonSoundModel::traced() const {
    const QJsonObject& audio = editing_ ? draft_ : session_->state().audio;
    if (audio != traceOf_ || trace_.isEmpty()) {
        trace_ = trace(audio, program_, kTraceFrames);
        traceOf_ = audio;
    }
    return trace_;
}

QVector<int> SolomonSoundModel::numbers() const {
    QVector<int> list;
    const QJsonObject effect =
        session_->state().audio.value(QStringLiteral("effects")).toArray().at(program_ - 1).toObject();
    for (const QJsonValue& value : effect.value(QStringLiteral("channels")).toArray()) {
        list << (value.toObject().value(QStringLiteral("selector")).toInt() & 7);
    }
    return list;
}

QString SolomonSoundModel::voiceOf(int number) const { return QString::fromLatin1(kVoices[(number >> 1) & 3]); }

QString SolomonSoundModel::title() const { return programLabel(program_); }

QVector<sdk::SequencerChannel> SolomonSoundModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const QJsonObject effect =
        session_->state().audio.value(QStringLiteral("effects")).toArray().at(program_ - 1).toObject();
    for (const QJsonValue& value : effect.value(QStringLiteral("channels")).toArray()) {
        const QJsonObject c = value.toObject();
        const int number = c.value(QStringLiteral("selector")).toInt() & 7;
        sdk::SequencerChannel channel;
        channel.id = QString::number(number);
        channel.label = QString::fromLatin1(kVoiceLabels[(number >> 1) & 3]);
        channel.detail = tr("virtual %1 · %2").arg(number).arg(c.value(QStringLiteral("stream")).toString());
        channel.color = kVoiceColors[(number >> 1) & 3];
        channel.muted = mutedVoices_.contains(voiceOf(number));
        result << channel;
    }
    return result;
}

void SolomonSoundModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < numbers().size()) {
        channel_ = channel;
        emit selectionChanged();
    }
}

void SolomonSoundModel::setChannelMuted(int channel, bool muted) {
    const QVector<int> list = numbers();
    if (channel < 0 || channel >= list.size()) return;
    const QString voice = voiceOf(list.at(channel));
    if (muted) mutedVoices_.insert(voice);
    else mutedVoices_.remove(voice);
    emit contentChanged();
}

int SolomonSoundModel::pitchOf(int number, int token) const {
    if (number >= kPitchedLimit) return kBase + (token & 0x0f);  // a noise period, shown as a row
    if ((token & 0x0f) >= kRest) return -1;
    // The triangle sounds an octave below a pulse with the same period.
    return kBase + (token & 0x0f) + 12 * ((token >> 4) & 7) - ((number >> 1) == 2 ? 12 : 0);
}

QVector<sdk::SequencerNote> SolomonSoundModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    const QVector<int> list = numbers();
    if (channel < 0 || channel >= list.size()) return result;
    const int number = list.at(channel);
    const QVector<SolomonNote> played = traced().value(number);
    for (int i = 0; i < played.size(); ++i) {
        const SolomonNote& n = played.at(i);
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(number).arg(i);
        note.start = n.start;
        note.length = std::max(1, n.length);
        note.pitch = pitchOf(number, n.token);
        note.pitchEditable = number < kPitchedLimit && note.pitch >= 0;
        note.lengthEditable = false;
        result << note;
    }
    return result;
}

bool SolomonSoundModel::find(const QString& id, int* number, int* index) const {
    *number = id.section(QLatin1Char(':'), 0, 0).toInt();
    *index = id.section(QLatin1Char(':'), 1).toInt();
    const QVector<SolomonNote> list = traced().value(*number);
    return *index >= 0 && *index < list.size();
}

void SolomonSoundModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void SolomonSoundModel::beginEdit(const QString& id) {
    selectNote(id);
    draft_ = session_->state().audio;
    editing_ = true;
}

bool SolomonSoundModel::editNote(const QString& id, int pitch, int) {
    int number = 0;
    int index = 0;
    if (!editing_ || !find(id, &number, &index) || number >= kPitchedLimit) return false;
    const SolomonNote note = traced().value(number).at(index);
    int relative = pitch - kBase + ((number >> 1) == 2 ? 12 : 0);
    if (relative < 0) return false;
    const int octave = relative / 12;
    if (octave > 7) return false;
    const int token = (octave << 4) | (relative % 12);
    if (token == note.token) return false;
    QJsonArray commands = draft_.value(QStringLiteral("commands")).toArray();
    QJsonObject command = commands.at(note.command).toObject();
    command.insert(QStringLiteral("value"), token);
    commands.replace(note.command, command);
    draft_.insert(QStringLiteral("commands"), commands);
    emit contentChanged();
    return true;
}

void SolomonSoundModel::endEdit() {
    if (!editing_) return;
    editing_ = false;
    const QJsonObject next = draft_;
    if (next != session_->state().audio) {
        session_->edit(
            tr("Edit a note of %1").arg(programLabel(program_)), [next](SolomonState& s) { s.audio = next; }, nullptr);
    }
    emit contentChanged();
}

void SolomonSoundModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
    QDir().mkpath(directory);
    const QString document = QDir(directory).filePath(QStringLiteral("solomon-audio.json"));
    const QString wav = QDir(directory).filePath(QStringLiteral("program-%1.wav").arg(program_));
    try {
        saveJson(document, session_->state().audio);
    } catch (const SolomonError& e) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("sound");
        d.message = QString::fromUtf8(e.what());
        host->report(d);
        return;
    }
    QStringList arguments{QStringLiteral("audio"),
                          QStringLiteral("render-solomon-wav"),
                          document,
                          QStringLiteral("--output"),
                          wav,
                          QStringLiteral("--effect"),
                          QString::number(program_),
                          QStringLiteral("--seconds"),
                          QStringLiteral("30")};
    for (const QString& voice : mutedVoices_) arguments << QStringLiteral("--mute") << voice;
    QString error;
    sdk::ProcessSpec spec = host->kitCommand(arguments, session_->projectDirectory(), &error);
    if (spec.program.isEmpty()) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("sound");
        d.message = error;
        host->report(d);
        return;
    }
    spec.label = tr("Render %1").arg(programLabel(program_));
    const QString label = programLabel(program_);
    host->runProcess(spec, [host, wav, label](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !QFileInfo(wav).isFile()) {
            if (!result.cancelled) {
                sdk::Diagnostic d;
                d.severity = sdk::Severity::Error;
                d.source = QStringLiteral("sound");
                d.message =
                    tr("Rendering %1 failed: %2")
                        .arg(label, QString::fromUtf8(result.standardError).trimmed().section(QLatin1Char('\n'), -1));
                host->report(d);
            }
            return;
        }
        host->playAudio(wav);
    });
}

void SolomonSoundModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool SolomonSoundModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void SolomonSoundModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

sdk::Budget SolomonSoundModel::budget() const {
    int notes = 0;
    for (const auto& list : traced()) notes += list.size();
    sdk::Budget budget;
    budget.text = tr("%1 notes in the first %2 s · stream bytes fixed").arg(notes).arg(kTraceFrames / 60);
    return budget;
}

QString SolomonSoundModel::statusText() const { return programLabel(program_); }

sdk::Inspection SolomonSoundModel::inspect() const {
    sdk::Inspection inspection;
    int number = 0;
    int index = 0;
    if (!selected_.isEmpty() && find(selected_, &number, &index)) {
        const SolomonNote note = traced().value(number).at(index);
        inspection.kicker = tr("Note");
        inspection.title =
            number >= kPitchedLimit ? tr("Noise %1").arg(nes::hex(note.token)) : pitchName(pitchOf(number, note.token));
        sdk::Field at;
        at.key = QStringLiteral("at");
        at.label = tr("Starts");
        at.value = tr("frame %1 · %2 s").arg(note.start).arg(note.start / 60.0, 0, 'f', 2);
        at.monospace = true;
        sdk::Field length;
        length.key = QStringLiteral("length");
        length.label = tr("Length");
        length.value = tr("%n frames", nullptr, note.length);
        length.monospace = true;
        inspection.fields = {at, length};
        inspection.note =
            tr("Drag the note up or down to change its pitch. The byte lives in shared "
               "stream storage: every repeat, loop and call that plays this command "
               "changes with it.");
        inspection.engineTag = QStringLiteral("solomon.audio@1");
        inspection.engine = {{tr("Command"), QString::number(note.command)}, {tr("Note byte"), nes::hex(note.token)}};
        return inspection;
    }
    inspection.kicker = tr("Audio program");
    inspection.title = programLabel(program_);
    inspection.note =
        tr("The sound engine runs up to eight virtual channels; each pair shares an APU "
           "voice and the first of a pair wins while it plays. Listen renders the "
           "program with the kit, unsaved edits included.");
    inspection.engineTag = QStringLiteral("solomon.audio@1");
    return inspection;
}

}  // namespace rl::solomon
