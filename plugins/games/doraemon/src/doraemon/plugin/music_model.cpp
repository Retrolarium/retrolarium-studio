#include "doraemon/plugin/music_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/plugin.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

namespace rl::doraemon {

namespace {

const char* const kChannels[4] = {"Pulse 1", "Pulse 2", "Triangle", "Noise"};
const QColor kColours[4] = {QColor(0xe0, 0xb0, 0x40), QColor(0x60, 0xa8, 0xe0), QColor(0x70, 0xc8, 0x70), QColor(0xb0, 0xb0, 0xb0)};

// Bytes an event text takes: "event $2B" and "duration $8E" are one byte,
// a command one plus its operands ("CallStream $CB $F0").
int widthOf(const QString& text) {
    if (text.startsWith(QLatin1String("event ")) || text.startsWith(QLatin1String("duration "))) return 1;
    return 1 + int(text.count(QLatin1Char('$')));
}

}  // namespace

QString DoraemonMusicModel::trackTitle(int driver, int track) {
    // The source reconstruction's evidence-backed track catalog.
    static const char* const kTitles[4][8] = {
        {"City - Starting Area", "Underground - Main", "Underground - Deep Rooms", "City - Cross Area", "Underground Finale",
         "Bull Robot Boss", "Player Defeat", "Chapter Clear"},
        {"Cave - Part 1", "Cave - Part 2", "Cave - Part 3", "Stage Boss", "Boss Defeated", "Player Defeat", nullptr, nullptr},
        {"Underwater - Upper Rooms", "Underwater - Middle Rooms", "Mid-Boss Formation", "Mid-Boss Alternate Mix",
         "Underwater - Lower Rooms", "Underwater - Final Room", "Chapter Clear", "Player Defeat"},
        {"Title Screen", "Ending - Opening", "Ending - Credits", "Game Over", nullptr, nullptr, nullptr, nullptr}};
    if (driver < 0 || driver > 3 || track < 0 || track > 7 || !kTitles[driver][track]) return tr("Track %1").arg(track + 1);
    return QString::fromLatin1(kTitles[driver][track]);
}

DoraemonMusicModel::DoraemonMusicModel(DoraemonSession* session, int driver, int track)
    : SequencerModel(session), session_(session), driver_(driver), track_(track) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &DoraemonSession::traceReady, this, &sdk::EditorModel::contentChanged);
}

QJsonObject DoraemonMusicModel::trace() const {
    return session_->musicTrace(driver_, track_);
}

QVector<sdk::SequencerChannel> DoraemonMusicModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    for (int c = 0; c < 4; ++c) {
        sdk::SequencerChannel channel;
        channel.id = QString::number(c);
        channel.label = QString::fromLatin1(kChannels[c]);
        channel.color = kColours[c];
        channel.muted = muted_.contains(c);
        result << channel;
    }
    return result;
}

void DoraemonMusicModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < 4) {
        channel_ = channel;
        emit selectionChanged();
    }
}

void DoraemonMusicModel::setChannelMuted(int channel, bool muted) {
    if (muted) muted_.insert(channel);
    else muted_.remove(channel);
    emit toolsChanged();
}

QVector<sdk::SequencerNote> DoraemonMusicModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    const QJsonArray events = trace().value(QStringLiteral("lanes")).toArray().at(channel).toObject().value(QStringLiteral("events")).toArray();
    for (int i = 0; i < events.size(); ++i) {
        const QJsonObject e = events.at(i).toObject();
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(channel).arg(i);
        note.start = e.value(QStringLiteral("start")).toInt();
        note.length = std::max(1, e.value(QStringLiteral("length")).toInt());
        int pitch = e.value(QStringLiteral("pitch")).toInt(-1);
        // Noise has no musical pitch: its notes sit on one row.
        if (channel == 3) pitch = pitch < 0 ? -1 : 36;
        if (note.id == dragging_ && pitch >= 0) pitch = dragBase_ + dragShift_;
        note.pitch = pitch < 0 ? -1 : std::clamp(pitch, pitchMinimum(), pitchMaximum());
        note.pitchEditable = channel < 3 && pitch >= 0;
        result << note;
    }
    return result;
}

QJsonObject DoraemonMusicModel::eventAt(const QString& noteId) const {
    const int channel = noteId.section(QLatin1Char(':'), 0, 0).toInt();
    const int index = noteId.section(QLatin1Char(':'), 1, 1).toInt();
    return trace().value(QStringLiteral("lanes")).toArray().at(channel).toObject().value(QStringLiteral("events")).toArray().at(index).toObject();
}

DoraemonMusicModel::Location DoraemonMusicModel::locate(int address) const {
    Location result;
    const QJsonArray segments =
        session_->music().value(QStringLiteral("drivers")).toArray().at(driver_).toObject().value(QStringLiteral("segments")).toArray();
    for (int s = 0; s < segments.size(); ++s) {
        const QJsonObject segment = segments.at(s).toObject();
        int cursor = segment.value(QStringLiteral("address")).toString().toInt(nullptr, 16);
        const QJsonArray events = segment.value(QStringLiteral("events")).toArray();
        for (int e = 0; e < events.size(); ++e) {
            const QString text = events.at(e).toString();
            if (cursor == address) {
                result.segment = s;
                result.event = e;
                result.text = text;
                return result;
            }
            cursor += widthOf(text);
        }
    }
    return result;
}

void DoraemonMusicModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        const int channel = id.section(QLatin1Char(':'), 0, 0).toInt();
        if (!id.isEmpty() && channel != channel_) channel_ = channel;
        emit selectionChanged();
    }
}

bool DoraemonMusicModel::shiftEvent(int address, int semitones, const QString& label, QString* error) {
    const Location at = locate(address);
    if (at.segment < 0 || !at.text.startsWith(QLatin1String("event $"))) {
        if (error) *error = tr("This note is not an event byte of the stream.");
        return false;
    }
    const int value = at.text.mid(7).toInt(nullptr, 16);
    const int next = value + semitones;
    if (value == 0 || next < 1 || next > 0x7f) {
        if (error) *error = tr("A note event is $01-$7F.");
        return false;
    }
    QJsonObject music = session_->music();
    QJsonArray drivers = music.value(QStringLiteral("drivers")).toArray();
    QJsonObject driver = drivers.at(driver_).toObject();
    QJsonArray segments = driver.value(QStringLiteral("segments")).toArray();
    QJsonObject segment = segments.at(at.segment).toObject();
    QJsonArray events = segment.value(QStringLiteral("events")).toArray();
    events.replace(at.event, QStringLiteral("event $%1").arg(QString::number(next, 16).rightJustified(2, QLatin1Char('0')).toUpper()));
    segment.insert(QStringLiteral("events"), events);
    segments.replace(at.segment, segment);
    driver.insert(QStringLiteral("segments"), segments);
    drivers.replace(driver_, driver);
    music.insert(QStringLiteral("drivers"), drivers);
    return session_->editMusic(label, music, error);
}

void DoraemonMusicModel::beginEdit(const QString& id) {
    selectNote(id);
    dragging_ = id;
    dragShift_ = 0;
    dragBase_ = eventAt(id).value(QStringLiteral("pitch")).toInt(-1);
}

bool DoraemonMusicModel::editNote(const QString& id, int pitch, int) {
    if (id != dragging_ || dragBase_ < 0 || id.section(QLatin1Char(':'), 0, 0).toInt() == 3) return false;
    const int shift = pitch - dragBase_;
    if (shift == dragShift_) return false;
    dragShift_ = shift;
    emit contentChanged();
    return true;
}

void DoraemonMusicModel::endEdit() {
    const QString id = dragging_;
    const int shift = dragShift_;
    dragging_.clear();
    dragShift_ = 0;
    if (!id.isEmpty() && shift != 0) {
        QString error;
        if (!shiftEvent(eventAt(id).value(QStringLiteral("address")).toInt(), shift, tr("Edit a note of %1").arg(title()), &error)) {
            session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        }
    }
    emit contentChanged();
}

bool DoraemonMusicModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_.isEmpty() || key != QLatin1String("value")) return SequencerModel::setField(key, value, error);
    const int address = eventAt(selected_).value(QStringLiteral("address")).toInt();
    const Location at = locate(address);
    if (!at.text.startsWith(QLatin1String("event $"))) return false;
    const int old = at.text.mid(7).toInt(nullptr, 16);
    return shiftEvent(address, std::clamp(value.toInt(), 1, 0x7f) - old, tr("Edit a note of %1").arg(title()), error);
}

void DoraemonMusicModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    QString error;
    const QString document = session_->writeMusicSnapshot(&error);
    if (document.isEmpty()) {
        session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return;
    }
    const QString wav = QDir(QFileInfo(document).absolutePath()).filePath(QStringLiteral("track-%1-%2.wav").arg(driver_).arg(track_));
    QStringList arguments{QStringLiteral("audio"), QStringLiteral("render-doraemon-wav"), session_->projectPath(),
                          QStringLiteral("--document"), document, QStringLiteral("--driver"), QString::number(driver_),
                          QStringLiteral("--track"), QString::number(track_), QStringLiteral("--frames"), QString::number(kFrames),
                          QStringLiteral("--output"), wav};
    if (!muted_.isEmpty()) {
        for (int c = 0; c < 4; ++c) {
            if (!muted_.contains(c)) arguments << QStringLiteral("--channel") << QString::fromLatin1(kChannels[c]);
        }
    }
    sdk::ProcessSpec spec = host->kitCommand(arguments, session_->projectDirectory(), &error);
    if (spec.program.isEmpty()) {
        session_->report(sdk::Severity::Error, QStringLiteral("sound"), error);
        return;
    }
    spec.label = tr("Render %1").arg(title());
    DoraemonSession* session = session_;
    const QString label = title();
    host->runProcess(spec, [host, session, wav, label](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !QFileInfo(wav).isFile()) {
            if (!result.cancelled) session->report(sdk::Severity::Error, QStringLiteral("sound"), tr("Rendering %1 failed.").arg(label));
            return;
        }
        host->playAudio(wav);
    });
}

void DoraemonMusicModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool DoraemonMusicModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void DoraemonMusicModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

sdk::Budget DoraemonMusicModel::budget() const {
    const QJsonObject driver = session_->music().value(QStringLiteral("drivers")).toArray().at(driver_).toObject();
    sdk::Budget budget;
    budget.text = tr("%1 · %2 stream bytes, each note keeps its byte")
                      .arg(driver.value(QStringLiteral("name")).toString())
                      .arg(driver.value(QStringLiteral("metrics")).toObject().value(QStringLiteral("reachable_bytes")).toInt());
    return budget;
}

sdk::Inspection DoraemonMusicModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.music@1");
    const QJsonObject e = selected_.isEmpty() ? QJsonObject() : eventAt(selected_);
    if (e.isEmpty()) {
        inspection.kicker = tr("Track");
        inspection.title = title();
        inspection.note = trace().isEmpty()
                              ? tr("Tracing the track…")
                              : tr("The track's four channels as the driver plays them, for 40 seconds. Drag a "
                                   "pulse or triangle note up or down to change its event byte: every pass "
                                   "of a loop that plays the byte follows. Listen renders the current "
                                   "document; muted channels stay silent.");
        return inspection;
    }
    const int address = e.value(QStringLiteral("address")).toInt();
    const Location at = locate(address);
    inspection.kicker = QString::fromLatin1(kChannels[selected_.section(QLatin1Char(':'), 0, 0).toInt() & 3]);
    inspection.title = tr("Frame %1").arg(e.value(QStringLiteral("start")).toInt());
    sdk::Field event;
    event.key = QStringLiteral("event");
    event.label = tr("Stream");
    event.value = tr("%1 at $%2").arg(at.text, QString::number(address, 16).rightJustified(4, QLatin1Char('0')).toUpper());
    event.monospace = true;
    inspection.fields = {event};
    if (at.text.startsWith(QLatin1String("event $")) && at.text != QLatin1String("event $00")) {
        sdk::Field value;
        value.key = QStringLiteral("value");
        value.label = tr("Value");
        value.type = sdk::Field::Type::Integer;
        value.value = at.text.mid(7).toInt(nullptr, 16);
        value.minimum = 1;
        value.maximum = 0x7f;
        value.hint = tr("semitones on tonal channels");
        value.monospace = true;
        inspection.fields << value;
    }
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    length.value = tr("%n frames", nullptr, e.value(QStringLiteral("length")).toInt());
    inspection.fields << length;
    inspection.note = tr("Durations are shared codes of the stream; notes keep their place.");
    return inspection;
}

QString DoraemonMusicModel::statusText() const {
    return tr("Sound · %1").arg(title());
}

}  // namespace rl::doraemon
