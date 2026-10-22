#include "pacman/plugin/sound_model.hpp"

#include "nes/editors.hpp"
#include "pacman/plugin/pacman_session.hpp"
#include "sdk/plugin.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace rl::pacman {

namespace {

constexpr int kBaseMidi = 45;  // pacman_sound.BASE_MIDI_NOTE

const QColor kColours[2] = {QColor(0xe0, 0xb0, 0x40), QColor(0x60, 0xa8, 0xe0)};

const QStringList kDuties{QStringLiteral("12.5%"), QStringLiteral("25%"), QStringLiteral("50%"), QStringLiteral("75%")};

}  // namespace

int PacmanSoundModel::pitchOf(int value) {
    if (value < 0 || value >= 0xc0) return -1;
    return kBaseMidi + (value >> 4) + 12 * (value & 0x0f);
}

int PacmanSoundModel::valueOf(int pitch) {
    const int delta = pitch - kBaseMidi;
    if (delta < 0 || delta > 11 + 12 * 15) return -1;
    return ((delta % 12) << 4) | (delta / 12);
}

int PacmanSoundModel::streamBytes(const QJsonObject& stream) {
    int bytes = 4 + stream.value(QStringLiteral("trailing_bytes")).toArray().size();
    for (const QJsonValue& v : stream.value(QStringLiteral("commands")).toArray()) {
        const QJsonObject c = v.toObject();
        const int opcode = c.value(QStringLiteral("opcode")).toInt();
        bytes += c.value(QStringLiteral("kind")).toString() == QLatin1String("control") &&
                         !(opcode >= 0xf1 && opcode <= 0xf6)
                     ? 1
                     : 2;
    }
    return bytes;
}

PacmanSoundModel::PacmanSoundModel(PacmanSession* session, QVector<int> streams, QString title)
    : SequencerModel(session), session_(session), slots_(std::move(streams)), title_(std::move(title)) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject PacmanSoundModel::sound() const { return editing_ ? draft_ : session_->resource(ids::kSound); }

QJsonObject PacmanSoundModel::stream(int channel) const {
    return sound()
        .value(QStringLiteral("slots"))
        .toArray()
        .at(slots_.value(channel))
        .toObject()
        .value(QStringLiteral("stream"))
        .toObject();
}

QVector<sdk::SequencerChannel> PacmanSoundModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const QJsonArray all = sound().value(QStringLiteral("slots")).toArray();
    for (int c = 0; c < slots_.size(); ++c) {
        sdk::SequencerChannel channel;
        channel.id = QString::number(slots_.at(c));
        channel.label = slots_.size() > 1 ? tr("Square %1").arg(c + 1) : QStringLiteral("Square");
        channel.detail = all.at(slots_.at(c)).toObject().value(QStringLiteral("label")).toString();
        channel.color = kColours[c % 2];
        result << channel;
    }
    return result;
}

void PacmanSoundModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < slots_.size()) {
        channel_ = channel;
        emit selectionChanged();
    }
}

QVector<sdk::SequencerNote> PacmanSoundModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    const QJsonArray commands = stream(channel).value(QStringLiteral("commands")).toArray();
    int time = 0;
    int pitch = -1;
    for (int i = 0; i < commands.size(); ++i) {
        const QJsonObject c = commands.at(i).toObject();
        const QString kind = c.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("control")) continue;
        const int duration = c.value(QStringLiteral("duration")).toInt();
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(channel).arg(i);
        note.start = time;
        note.length = std::max(1, duration);
        note.lengthEditable = true;
        if (kind == QLatin1String("note")) {
            pitch = pitchOf(c.value(QStringLiteral("value")).toInt());
            note.pitchEditable = true;
        } else {
            // A duration marker holds the note before it.
            note.pitchEditable = false;
        }
        note.pitch = pitch;
        result << note;
        time += duration;
    }
    return result;
}

void PacmanSoundModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        const int channel = id.section(QLatin1Char(':'), 0, 0).toInt();
        if (!id.isEmpty() && channel != channel_ && channel < slots_.size()) channel_ = channel;
        emit selectionChanged();
    }
}

QJsonObject PacmanSoundModel::withCommand(const QJsonObject& from, int channel, int command,
                                          const QJsonObject& value) const {
    QJsonObject next = from;
    QJsonArray all = next.value(QStringLiteral("slots")).toArray();
    QJsonObject slot = all.at(slots_.at(channel)).toObject();
    QJsonObject s = slot.value(QStringLiteral("stream")).toObject();
    QJsonArray commands = s.value(QStringLiteral("commands")).toArray();
    commands.replace(command, value);
    s.insert(QStringLiteral("commands"), commands);
    slot.insert(QStringLiteral("stream"), s);
    all.replace(slots_.at(channel), slot);
    next.insert(QStringLiteral("slots"), all);
    return next;
}

QJsonObject PacmanSoundModel::withPrologue(const QJsonObject& from, int channel, int index, int value) const {
    QJsonObject next = from;
    QJsonArray all = next.value(QStringLiteral("slots")).toArray();
    QJsonObject slot = all.at(slots_.at(channel)).toObject();
    QJsonObject s = slot.value(QStringLiteral("stream")).toObject();
    QJsonArray prologue = s.value(QStringLiteral("prologue")).toArray();
    prologue.replace(index, value);
    s.insert(QStringLiteral("prologue"), prologue);
    slot.insert(QStringLiteral("stream"), s);
    all.replace(slots_.at(channel), slot);
    next.insert(QStringLiteral("slots"), all);
    return next;
}

bool PacmanSoundModel::commit(const QString& text, const QJsonObject& next, QString* error) {
    int used = 0;
    for (const QJsonValue& slot : next.value(QStringLiteral("slots")).toArray()) {
        used += streamBytes(slot.toObject().value(QStringLiteral("stream")).toObject());
    }
    if (used > next.value(QStringLiteral("capacity")).toInt()) {
        if (error) *error = tr("The sound arena holds %1 bytes.").arg(next.value(QStringLiteral("capacity")).toInt());
        return false;
    }
    return session_->editBundle(text, [next](nes::Bundle& b) { b.setResource(ids::kSound, next); }, error);
}

void PacmanSoundModel::beginEdit(const QString& id) {
    selectNote(id);
    draft_ = session_->resource(ids::kSound);
    editing_ = true;
}

bool PacmanSoundModel::editNote(const QString& id, int pitch, int length) {
    if (!editing_) return false;
    const int channel = id.section(QLatin1Char(':'), 0, 0).toInt();
    const int command = id.section(QLatin1Char(':'), 1, 1).toInt();
    if (channel < 0 || channel >= slots_.size()) return false;
    QJsonObject c = stream(channel).value(QStringLiteral("commands")).toArray().at(command).toObject();
    const QJsonObject before = c;
    if (c.value(QStringLiteral("kind")).toString() == QLatin1String("note")) {
        const int value = valueOf(pitch);
        if (value >= 0) c.insert(QStringLiteral("value"), value);
    }
    if (length > 0) c.insert(QStringLiteral("duration"), std::clamp(length, 1, 255));
    if (c == before) return false;
    draft_ = withCommand(draft_, channel, command, c);
    emit contentChanged();
    return true;
}

void PacmanSoundModel::endEdit() {
    if (!editing_) return;
    editing_ = false;
    const QJsonObject next = draft_;
    if (next != session_->resource(ids::kSound)) {
        QString error;
        if (!commit(tr("Edit a note of %1").arg(title_), next, &error)) {
            session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        }
    }
    emit contentChanged();
}

bool PacmanSoundModel::setField(const QString& key, const QVariant& value, QString* error) {
    const QJsonObject now = session_->resource(ids::kSound);
    if (key == QLatin1String("duty") || key == QLatin1String("volume")) {
        const int reg = stream(channel_).value(QStringLiteral("prologue")).toArray().at(1).toInt();
        int next = reg;
        if (key == QLatin1String("duty"))
            next = (reg & 0x3f) | (std::max(0, int(kDuties.indexOf(value.toString()))) << 6);
        else next = (reg & 0xf0) | std::clamp(value.toInt(), 0, 15);
        return commit(tr("Set the voice of %1").arg(title_), withPrologue(now, channel_, 1, next), error);
    }
    const int channel = selected_.section(QLatin1Char(':'), 0, 0).toInt();
    const int command = selected_.section(QLatin1Char(':'), 1, 1).toInt();
    if (selected_.isEmpty() || channel >= slots_.size()) return SequencerModel::setField(key, value, error);
    QJsonObject c = stream(channel).value(QStringLiteral("commands")).toArray().at(command).toObject();
    if (key == QLatin1String("duration")) {
        c.insert(QStringLiteral("duration"), std::clamp(value.toInt(), 1, 255));
    } else if (key == QLatin1String("pitch") && c.value(QStringLiteral("kind")).toString() == QLatin1String("note")) {
        int pitch = -1;
        for (int p = pitchMinimum(); p <= pitchMaximum(); ++p) {
            if (pitchName(p) == value.toString()) pitch = p;
        }
        if (valueOf(pitch) < 0) {
            if (error) *error = tr("Pick a note from A2 up.");
            return false;
        }
        c.insert(QStringLiteral("value"), valueOf(pitch));
    } else {
        return SequencerModel::setField(key, value, error);
    }
    return commit(tr("Edit a note of %1").arg(title_), withCommand(now, channel, command, c), error);
}

void PacmanSoundModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
    QDir().mkpath(directory);
    const QString document = QDir(directory).filePath(QStringLiteral("pacman-sound.json"));
    const QString wav = QDir(directory).filePath(QStringLiteral("slot-%1.wav").arg(slots_.value(0)));
    QSaveFile file(document);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(session_->bundle().root()).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        session_->report(sdk::Severity::Error, QStringLiteral("sound"), tr("Cannot write %1.").arg(document));
        return;
    }
    QStringList arguments{QStringLiteral("audio"),
                          QStringLiteral("render-wav"),
                          document,
                          QStringLiteral("--output"),
                          wav,
                          QStringLiteral("--resource"),
                          ids::kSound,
                          QStringLiteral("--slot"),
                          QString::number(slots_.value(0))};
    for (int c = 1; c < slots_.size(); ++c) arguments << QStringLiteral("--mix-slot") << QString::number(slots_.at(c));
    QString error;
    sdk::ProcessSpec spec = host->kitCommand(arguments, session_->projectDirectory(), &error);
    if (spec.program.isEmpty()) {
        session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return;
    }
    spec.label = tr("Render %1").arg(title_);
    PacmanSession* session = session_;
    const QString label = title_;
    host->runProcess(spec, [host, session, wav, label](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !QFileInfo(wav).isFile()) {
            if (!result.cancelled) {
                session->report(sdk::Severity::Error, QStringLiteral("sound"), tr("Rendering %1 failed.").arg(label));
            }
            return;
        }
        host->playAudio(wav);
    });
}

void PacmanSoundModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool PacmanSoundModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void PacmanSoundModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

sdk::Budget PacmanSoundModel::budget() const {
    const QJsonObject s = sound();
    int used = 0;
    int mine = 0;
    const QJsonArray all = s.value(QStringLiteral("slots")).toArray();
    for (int i = 0; i < all.size(); ++i) {
        const int bytes = streamBytes(all.at(i).toObject().value(QStringLiteral("stream")).toObject());
        used += bytes;
        if (slots_.contains(i)) mine += bytes;
    }
    sdk::Budget budget;
    budget.used = used;
    budget.capacity = s.value(QStringLiteral("capacity")).toInt();
    budget.text = tr("this %1 B · all sounds %2 / %3 bytes").arg(mine).arg(used).arg(budget.capacity);
    return budget;
}

sdk::Inspection PacmanSoundModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("pacman.sound-streams.v1");
    const int channel = selected_.isEmpty() ? -1 : selected_.section(QLatin1Char(':'), 0, 0).toInt();
    const int command = selected_.section(QLatin1Char(':'), 1, 1).toInt();
    const QJsonObject c = channel >= 0
                              ? stream(channel).value(QStringLiteral("commands")).toArray().at(command).toObject()
                              : QJsonObject();
    if (c.isEmpty()) {
        const int reg = stream(channel_).value(QStringLiteral("prologue")).toArray().at(1).toInt();
        inspection.kicker = slots_.size() > 1 ? tr("Tune") : tr("Sound effect");
        inspection.title = title_;
        sdk::Field duty;
        duty.key = QStringLiteral("duty");
        duty.label = tr("Duty");
        duty.type = sdk::Field::Type::Choice;
        duty.choices = kDuties;
        duty.value = kDuties.at((reg >> 6) & 3);
        duty.hint = channels().value(channel_).label;
        sdk::Field volume;
        volume.key = QStringLiteral("volume");
        // Bit 4 picks a constant volume; without it the low bits are the
        // envelope's decay period, restarted by every note.
        volume.label = (reg & 0x10) ? tr("Volume") : tr("Decay");
        volume.hint = (reg & 0x10) ? QStringLiteral("0-15") : tr("slow 15 · 0 fast");
        volume.type = sdk::Field::Type::Integer;
        volume.value = reg & 0x0f;
        volume.minimum = 0;
        volume.maximum = 15;
        inspection.fields = {duty, volume};
        inspection.note =
            tr("Notes in frames (60 a second), in the order the stream plays them. Drag a "
               "note up or down to retune it, stretch it to change its duration; grey notes "
               "hold the note before them. Duty and volume (or the decay of each note's "
               "envelope) are the voice the stream starts with; the sounds share one arena.");
        return inspection;
    }
    const bool note = c.value(QStringLiteral("kind")).toString() == QLatin1String("note");
    inspection.kicker = note ? tr("Note") : tr("Hold");
    inspection.title = tr("%1 · step %2").arg(channels().value(channel).label).arg(command + 1);
    sdk::Field duration;
    duration.key = QStringLiteral("duration");
    duration.label = tr("Duration");
    duration.type = sdk::Field::Type::Integer;
    duration.value = c.value(QStringLiteral("duration")).toInt();
    duration.minimum = 1;
    duration.maximum = 255;
    duration.hint = QStringLiteral("frames");
    if (note) {
        sdk::Field pitch;
        pitch.key = QStringLiteral("pitch");
        pitch.label = tr("Pitch");
        pitch.type = sdk::Field::Type::Choice;
        for (int p = pitchMinimum(); p <= pitchMaximum(); ++p) pitch.choices << pitchName(p);
        pitch.value = pitchName(pitchOf(c.value(QStringLiteral("value")).toInt()));
        pitch.hint = nes::hex(c.value(QStringLiteral("value")).toInt());
        inspection.fields = {pitch, duration};
    } else {
        inspection.fields = {duration};
        inspection.note = tr("A duration marker: the note before it sounds on for these frames.");
    }
    return inspection;
}

QString PacmanSoundModel::statusText() const { return tr("Sound · %1").arg(title_); }

}  // namespace rl::pacman
