#include "alien_soldier/plugin/sound_model.hpp"

#include "alien_soldier/data/documents.hpp"
#include "alien_soldier/plugin/session.hpp"

#include "sdk/plugin.hpp"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

// Operand counts of $E0-$FE ($E4 decides its own; see sound.py in the kit).
int operandCount(const QByteArray& data, int at) {
    static const int counts[] = {1, 1, 1, 0, -1, 2, 1, 0, 1, 2, 1, 1, 1, 2, 2, 1,
                                 4, 2, 0, 1, 1,  1, 2, 4, 2, 0, 1, 1, 0, 0, 4};
    const int opcode = quint8(data.at(at));
    if (opcode == 0xFF) {
        static const int extended[] = {4, 1, 1, 2, 0};
        const int selector = at + 1 < data.size() ? quint8(data.at(at + 1)) : 0;
        return 1 + (selector < 5 ? extended[selector] : 0);
    }
    const int count = counts[opcode - 0xE0];
    if (count >= 0) return count;
    return at + 1 < data.size() && quint8(data.at(at + 1)) == 0 ? 1 : 5;
}

int signedWord(const QByteArray& data, int at) { return qint16((quint8(data.at(at)) << 8) | quint8(data.at(at + 1))); }

const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

}  // namespace

SoundModel::SoundModel(AlienSoldierSession* session, const QString& label)
    : SequencerModel(session), session_(session), label_(label) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Sound) emit contentChanged();
    });
}

QJsonObject SoundModel::payload() const {
    const QJsonArray payloads = session_->doc(Doc::Sound).value(QStringLiteral("payloads")).toArray();
    return payloads.at(indexOfLabel(payloads, label_)).toObject();
}

QByteArray SoundModel::bytes() const { return hexBytes(payload().value(QStringLiteral("data")).toString()); }

QString SoundModel::chipLabel() const { return tr("YM2612 + PSG"); }

QVector<sdk::SequencerChannel> SoundModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const QJsonObject item = payload();
    const QJsonArray list = item.value(QStringLiteral("channels")).toArray();
    static const QColor colors[] = {QColor(0xE0, 0x6C, 0x75), QColor(0x61, 0xAF, 0xEF), QColor(0x98, 0xC3, 0x79),
                                    QColor(0xE5, 0xC0, 0x7B), QColor(0xC6, 0x78, 0xDD), QColor(0x56, 0xB6, 0xC2)};
    for (int index = 0; index < list.size(); ++index) {
        const int type = list.at(index).toObject().value(QStringLiteral("type")).toInt();
        sdk::SequencerChannel channel;
        channel.id = QString::number(index);
        if (type == 6) channel.label = tr("DAC");
        else if (type & 0x80) channel.label = type == 0xE0 ? tr("Noise") : tr("PSG %1").arg(((type >> 5) & 3) + 1);
        else channel.label = tr("FM %1").arg((type & 3) + 1 + ((type & 4) ? 3 : 0));
        channel.detail =
            QStringLiteral("$%1")
                .arg(list.at(index).toObject().value(QStringLiteral("sequence")).toInt(), 4, 16, QLatin1Char('0'))
                .toUpper();
        channel.color = colors[index % 6];
        channel.muted = muted_.contains(index);
        result.append(channel);
    }
    return result;
}

void SoundModel::setCurrentChannel(int channel) {
    channel_ = channel;
    note_.clear();
    emit selectionChanged();
}

void SoundModel::setChannelMuted(int channel, bool muted) {
    if (muted) muted_.insert(channel);
    else muted_.remove(channel);
    emit contentChanged();
}

QVector<SoundModel::Event> SoundModel::walk(int channel) const {
    if (cacheRevision_ != session_->revision(Doc::Sound)) {
        cache_.clear();
        cacheRevision_ = session_->revision(Doc::Sound);
    }
    const auto cached = cache_.constFind(channel);
    if (cached != cache_.constEnd()) return cached.value();
    QVector<Event> events;
    const QJsonObject item = payload();
    const QByteArray data = bytes();
    const QJsonObject header = item.value(QStringLiteral("channels")).toArray().at(channel).toObject();
    const int multiplier = std::max(1, item.value(QStringLiteral("tick_multiplier")).toInt(1));
    int at = header.value(QStringLiteral("sequence")).toInt();
    int transpose = qint8(header.value(QStringLiteral("initial")).toInt() >> 8);
    int duration = 1, time = 0, note = 0x80;
    QVector<int> returns;
    QHash<int, int> loops;
    QSet<QPair<int, int>> jumps;  // (target, call depth) jumped to already
    for (int steps = 0; steps < 20000 && at >= 0 && at < data.size() && events.size() < 4000; ++steps) {
        const int opcode = quint8(data.at(at));
        if (opcode < 0x80) {  // a duration: the last note again, held this long
            duration = opcode;
            events.append({-1, at, time, std::max(1, duration * multiplier), note, transpose});
            time += duration * multiplier;
            at += 1;
            continue;
        }
        if (opcode < 0xE0) {
            note = opcode;
            Event event{at, -1, time, 0, note, transpose};
            at += 1;
            if (at < data.size() && quint8(data.at(at)) < 0x80) {
                duration = quint8(data.at(at));
                event.durationOffset = at;
                at += 1;
            }
            event.length = std::max(1, duration * multiplier);
            events.append(event);
            time += duration * multiplier;
            continue;
        }
        const int size = 1 + operandCount(data, at);
        if (opcode == 0xE3 || opcode == 0xF2) break;
        if (opcode == 0xF9) {
            if (returns.isEmpty()) break;
            at = returns.takeLast();
            continue;
        }
        if (opcode == 0xFB) transpose += qint8(quint8(data.at(at + 1)));
        if (opcode == 0xF6 || opcode == 0xF8) {
            const int target = at + 2 + signedWord(data, at + 1);
            if (opcode == 0xF8) {
                returns.append(at + size);
            } else if (jumps.contains({target, int(returns.size())})) {
                break;  // the song starts over: once through is the roll
            } else {
                jumps.insert({target, int(returns.size())});
            }
            at = target;
            continue;
        }
        if (opcode == 0xF7) {
            const int index = quint8(data.at(at + 1));
            const int count = quint8(data.at(at + 2));
            int remaining = loops.contains(index) ? loops.value(index) : (count ? count : 256);
            remaining -= 1;
            if (remaining > 0) {
                loops.insert(index, remaining);
                at = at + 4 + signedWord(data, at + 3);
            } else {
                loops.remove(index);
                at += size;
            }
            continue;
        }
        at += size;
    }
    cache_.insert(channel, events);
    return events;
}

QVector<sdk::SequencerNote> SoundModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    for (const Event& event : walk(channel)) {
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2:%3")
                      .arg(channel)
                      .arg(event.offset >= 0 ? event.offset : event.durationOffset)
                      .arg(event.start);
        note.start = event.start;
        note.length = event.length;
        note.pitch = event.note == 0x80 ? -1 : std::clamp(event.note - 0x81 + event.transpose, 0, 95);
        note.pitchEditable = event.offset >= 0 && event.note != 0x80;
        note.lengthEditable = event.durationOffset >= 0;
        result.append(note);
    }
    return result;
}

QString SoundModel::pitchName(int pitch) const {
    return QStringLiteral("%1%2").arg(QLatin1String(kNames[pitch % 12])).arg(pitch / 12);
}

std::optional<SoundModel::Event> SoundModel::eventOf(const QString& id) const {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 3) return std::nullopt;
    for (const Event& event : walk(parts.at(0).toInt())) {
        if ((event.offset >= 0 ? event.offset : event.durationOffset) == parts.at(1).toInt() &&
            event.start == parts.at(2).toInt()) {
            return event;
        }
    }
    return std::nullopt;
}

void SoundModel::selectNote(const QString& id) {
    note_ = id;
    emit selectionChanged();
}

void SoundModel::beginEdit(const QString& id) {
    selectNote(id);
    editBefore_ = session_->doc(Doc::Sound);
    editRevision_ = session_->revision(Doc::Sound);
}

bool SoundModel::editNote(const QString& id, int pitch, int length) {
    const std::optional<Event> event = eventOf(id);
    if (!event) return false;
    QByteArray data = bytes();
    if (event->offset >= 0 && event->note != 0x80 && pitch >= 0) {
        const int value = pitch + 0x81 - event->transpose;
        if (value < 0x81 || value > 0xDF) return false;
        data[event->offset] = char(value);
    }
    if (event->durationOffset >= 0 && length > 0) {
        const int multiplier = std::max(1, payload().value(QStringLiteral("tick_multiplier")).toInt(1));
        data[event->durationOffset] = char(std::clamp(length / multiplier, 1, 0x7F));
    }
    QJsonObject& document = session_->live(Doc::Sound);
    QJsonArray payloads = document.value(QStringLiteral("payloads")).toArray();
    const int at = indexOfLabel(payloads, label_);
    QJsonObject item = payloads.at(at).toObject();
    item.insert(QStringLiteral("data"), bytesHex(data));
    payloads.replace(at, item);
    document.insert(QStringLiteral("payloads"), payloads);
    session_->touch(Doc::Sound);
    return true;
}

void SoundModel::endEdit() { session_->commit(Doc::Sound, editBefore_, editRevision_, tr("Edit note")); }

void SoundModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    QStringList arguments{QStringLiteral("studio"),
                          QStringLiteral("alien-soldier-sound-preview"),
                          session_->projectPath(),
                          QStringLiteral("--label"),
                          label_,
                          QStringLiteral("--seconds"),
                          QStringLiteral("30")};
    if (!muted_.isEmpty()) {
        for (int index = 0; index < channels().size(); ++index) {
            if (!muted_.contains(index)) arguments << QStringLiteral("--channel") << QString::number(index);
        }
    }
    // Unsaved edits are rendered from a copy of the sound document.
    if (session_->isDirty(Doc::Sound)) {
        const QByteArray json = QJsonDocument(session_->doc(Doc::Sound)).toJson(QJsonDocument::Compact);
        const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
        QDir().mkpath(directory);
        const QString hash =
            QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex().left(12));
        const QString path = QDir(directory).filePath(QStringLiteral(".alien-soldier-unsaved-%1.json").arg(hash));
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(json);
            file.commit();
        }
        arguments << QStringLiteral("--document-override") << path;
    }
    QString error;
    sdk::ProcessSpec spec = host->kitCommand(arguments, session_->projectDirectory(), &error);
    if (spec.program.isEmpty()) {
        session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return;
    }
    spec.label = tr("Render %1").arg(label_);
    // The kit hears the VGM through the studio's YM2612 renderer.
    const QString renderer = host->toolPath(QStringLiteral("retrolarium-ymfm-renderer"));
    if (!renderer.isEmpty()) spec.environment.insert(QStringLiteral("RETROLARIUM_YMFM_RENDERER"), renderer);
    const QString directory = session_->projectDirectory();
    const QString label = label_;
    QPointer<AlienSoldierSession> session = session_;
    // The model may close before rendering ends; capture values only.
    host->runProcess(spec, [host, session, directory, label](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled && session) {
                session->report(sdk::Severity::Error, QStringLiteral("sound"),
                                tr("Sound preview failed: %1").arg(sdk::failureText(result)));
            }
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        QString wav = json.value(QStringLiteral("wav")).toObject().value(QStringLiteral("path")).toString();
        if (QFileInfo(wav).isRelative()) wav = QDir(directory).filePath(wav);
        if (!QFileInfo(wav).isFile()) {
            if (session) {
                session->report(sdk::Severity::Error, QStringLiteral("sound"),
                                tr("The kit reported no WAV for %1.").arg(label));
            }
            return;
        }
        host->log(tr("  Playing %1").arg(QDir::toNativeSeparators(wav)));
        host->playAudio(wav);
    });
}

void SoundModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool SoundModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void SoundModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

sdk::Budget SoundModel::budget() const {
    sdk::Budget budget;
    budget.text = tr("%1 bytes · fixed").arg(bytes().size());
    return budget;
}

QString SoundModel::statusText() const { return tr("%1 · %2").arg(label_, channels().value(channel_).label); }

sdk::Inspection SoundModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject item = payload();
    inspection.kicker =
        item.value(QStringLiteral("kind")).toString() == QLatin1String("bgm") ? tr("Music") : tr("Sound effect");
    inspection.title = label_;
    QStringList requests;
    for (const QJsonValue& request : item.value(QStringLiteral("requests")).toArray()) {
        requests << QStringLiteral("$%1").arg(request.toInt(), 2, 16, QLatin1Char('0')).toUpper();
    }
    sdk::Field ids;
    ids.key = QStringLiteral("requests");
    ids.label = tr("Requests");
    ids.value = requests.join(QLatin1Char(' '));
    sdk::Field tempo;
    tempo.key = QStringLiteral("tempo");
    tempo.label = tr("Tempo");
    tempo.value = item.value(QStringLiteral("tempo")).toInt();
    sdk::Field instruments;
    instruments.key = QStringLiteral("instruments");
    instruments.label = tr("FM instruments");
    instruments.value = item.value(QStringLiteral("instrument_count")).toInt();
    inspection.fields = {ids, tempo, instruments};
    inspection.note =
        tr("Notes and durations change where the driver reads them; the payload keeps its size and every "
           "instruction its place, and the kit checks the edit before it builds.");
    inspection.engineTag = QStringLiteral("alien-soldier.sound.v1");
    return inspection;
}

}  // namespace rl::alien_soldier
