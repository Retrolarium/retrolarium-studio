#include "castle/plugin/music_models.hpp"

#include "castle/plugin/castle_session.hpp"
#include "sdk/plugin.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace rl::castle {

namespace {

constexpr int kTraceFrames = 60 * 60;       // a minute of a track set

const char* const kChannelLabels[3] = {"Pulse 1", "Pulse 2", "Triangle"};
const char* const kChannelVoices[3] = {"pulse1", "pulse2", "triangle"};
const QColor kChannelColors[3] = {QColor(0x56, 0xb4, 0xe9), QColor(0xe6, 0x9f, 0x00), QColor(0x00, 0x9e, 0x73)};

QString streamText(const QJsonObject& music, int stream) {
    return music.value(QStringLiteral("streams")).toArray().at(stream).toObject().value(QStringLiteral("text")).toString();
}

QString streamId(const QJsonObject& music, int stream) {
    return music.value(QStringLiteral("streams")).toArray().at(stream).toObject().value(QStringLiteral("id")).toString();
}

void report(CastleSession* session, const QString& message) {
    if (sdk::Host* host = session->host()) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("music");
        d.message = message;
        host->report(d);
    }
}

}  // namespace

// ---------------------------------------------------------------- track sets

CastleMusicModel::CastleMusicModel(CastleSession* session, int track)
    : SequencerModel(session), session_(session), track_(track) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject CastleMusicModel::music() const {
    return editing_ ? draft_ : session_->music();
}

const QVector<QVector<CastleMusicNote>>& CastleMusicModel::traced() const {
    const QString key = editing_ ? QStringLiteral("draft:%1").arg(draftVersion_)
                                 : QStringLiteral("saved:%1").arg(session_->revision());
    if (key != traceKey_) {
        trace_ = CastleMusic::trace(music(), track_, kTraceFrames);
        traceKey_ = key;
    }
    return trace_;
}

QJsonObject CastleMusicModel::trackSet() const {
    return session_->music().value(QStringLiteral("track_sets")).toArray().at(track_).toObject();
}

QString CastleMusicModel::title() const {
    return QStringLiteral("%1 · %2").arg(track_).arg(trackSet().value(QStringLiteral("name")).toString());
}

QVector<sdk::SequencerChannel> CastleMusicModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const QJsonArray streams = trackSet().value(QStringLiteral("streams")).toArray();
    for (int c = 0; c < CastleMusic::kChannels; ++c) {
        sdk::SequencerChannel channel;
        channel.id = QString::fromLatin1(kChannelVoices[c]);
        channel.label = QString::fromLatin1(kChannelLabels[c]);
        channel.detail = streams.at(c).isString() ? streams.at(c).toString() : QStringLiteral("silent");
        channel.color = kChannelColors[c];
        channel.muted = muted_.contains(c);
        result << channel;
    }
    return result;
}

void CastleMusicModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < CastleMusic::kChannels) {
        channel_ = channel;
        emit selectionChanged();
    }
}

void CastleMusicModel::setChannelMuted(int channel, bool muted) {
    if (channel < 0 || channel >= CastleMusic::kChannels) return;
    if (muted) muted_.insert(channel);
    else muted_.remove(channel);
    emit contentChanged();
}

QVector<sdk::SequencerNote> CastleMusicModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    if (channel < 0 || channel >= CastleMusic::kChannels) return result;
    const QVector<CastleMusicNote>& lane = traced().at(channel);
    for (int i = 0; i < lane.size(); ++i) {
        const CastleMusicNote& n = lane.at(i);
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(channel).arg(i);
        note.start = n.start;
        note.length = n.length;
        note.pitch = n.pitch;
        note.pitchEditable = n.pitch >= 0;
        note.lengthEditable = false;
        result << note;
    }
    return result;
}

bool CastleMusicModel::find(const QString& id, int* channel, int* index) const {
    *channel = id.section(QLatin1Char(':'), 0, 0).toInt();
    *index = id.section(QLatin1Char(':'), 1).toInt();
    return *channel >= 0 && *channel < CastleMusic::kChannels && *index >= 0 &&
           *index < traced().at(*channel).size();
}

void CastleMusicModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void CastleMusicModel::beginEdit(const QString& id) {
    selectNote(id);
    draft_ = session_->music();
    ++draftVersion_;
    editing_ = true;
}

bool CastleMusicModel::editNote(const QString& id, int pitch, int) {
    int channel = 0;
    int index = 0;
    if (!editing_ || !find(id, &channel, &index)) return false;
    const CastleMusicNote note = traced().at(channel).at(index);
    if (note.pitch < 0 || note.pitch == pitch) return false;
    const QString text = streamText(draft_, note.stream);
    const QString retuned = CastleMusic::retune(draft_, note, channel, pitch, text);
    if (retuned.isEmpty()) return false;
    try {
        draft_ = CastleMusic::withText(draft_, note.stream, retuned);
        ++draftVersion_;
    } catch (const CastleError&) {
        return false;      // the arena has no room for the octave change
    }
    emit contentChanged();
    return true;
}

void CastleMusicModel::endEdit() {
    if (!editing_) return;
    editing_ = false;
    const QJsonObject next = draft_;
    if (next != session_->music()) {
        QString error;
        if (!session_->editMusic(QStringLiteral("Edit a note of %1").arg(title()), next, &error)) {
            report(session_, error);
        }
    }
    emit contentChanged();
}

bool CastleMusicModel::setField(const QString& key, const QVariant& value, QString* error) {
    QJsonObject next = session_->music();
    QJsonArray sets = next.value(QStringLiteral("track_sets")).toArray();
    QJsonObject set = sets.at(track_).toObject();
    if (key.startsWith(QLatin1String("stream"))) {
        const int channel = key.mid(6).toInt();
        QJsonArray streams = set.value(QStringLiteral("streams")).toArray();
        const QString chosen = value.toString();
        streams.replace(channel, chosen.startsWith(QLatin1String("stream_")) ? QJsonValue(chosen) : QJsonValue());
        set.insert(QStringLiteral("streams"), streams);
    } else if (key.startsWith(QLatin1String("parameter"))) {
        const int channel = key.mid(9).toInt();
        QJsonArray parameters = set.value(QStringLiteral("channel_parameters")).toArray();
        parameters.replace(channel, std::clamp(value.toInt(), 0, 255));
        set.insert(QStringLiteral("channel_parameters"), parameters);
    } else {
        return SequencerModel::setField(key, value, error);
    }
    sets.replace(track_, set);
    next.insert(QStringLiteral("track_sets"), sets);
    return session_->editMusic(QStringLiteral("Edit %1").arg(title()), next, error);
}

void CastleMusicModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
    QDir().mkpath(directory);
    const QString document = QDir(directory).filePath(QStringLiteral("castle-music.json"));
    const QString wav = QDir(directory).filePath(QStringLiteral("track-%1.wav").arg(track_));
    QSaveFile file(document);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(session_->music()).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        report(session_, QStringLiteral("Cannot write %1.").arg(document));
        return;
    }
    QStringList arguments{QStringLiteral("audio"), QStringLiteral("render-castle-wav"), document,
                          QStringLiteral("--output"), wav, QStringLiteral("--track"), QString::number(track_),
                          QStringLiteral("--seconds"), QStringLiteral("40")};
    for (int c : muted_) arguments << QStringLiteral("--mute") << QString::fromLatin1(kChannelVoices[c]);
    QString error;
    sdk::ProcessSpec spec = host->kitCommand(arguments, session_->projectDirectory(), &error);
    if (spec.program.isEmpty()) {
        report(session_, error);
        return;
    }
    spec.label = QStringLiteral("Render %1").arg(title());
    const QString label = title();
    CastleSession* session = session_;
    host->runProcess(spec, [host, session, wav, label](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !QFileInfo(wav).isFile()) {
            if (!result.cancelled) {
                report(session, QStringLiteral("Rendering %1 failed: %2")
                                    .arg(label, QString::fromUtf8(result.standardError).trimmed().section(QLatin1Char('\n'), -1)));
            }
            return;
        }
        host->playAudio(wav);
    });
}

void CastleMusicModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool CastleMusicModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void CastleMusicModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

sdk::Budget CastleMusicModel::budget() const {
    const QJsonObject& m = session_->music();
    sdk::Budget budget;
    budget.used = CastleMusic::usedBytes(m);
    budget.capacity = m.value(QStringLiteral("capacity_bytes")).toInt();
    budget.text = QStringLiteral("MML %1 / %2 bytes").arg(budget.used).arg(budget.capacity);
    return budget;
}

QString CastleMusicModel::statusText() const {
    return QStringLiteral("Music · %1").arg(title());
}

sdk::Inspection CastleMusicModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle.mml-music.v1");
    int channel = 0;
    int index = 0;
    if (!selected_.isEmpty() && find(selected_, &channel, &index)) {
        const CastleMusicNote note = traced().at(channel).at(index);
        const QString text = streamText(music(), note.stream);
        inspection.kicker = QStringLiteral("%1 note").arg(QString::fromLatin1(kChannelLabels[channel]));
        inspection.title = note.pitch < 0 ? QStringLiteral("Rest") : pitchName(note.pitch);
        sdk::Field at;
        at.key = QStringLiteral("at");
        at.label = QStringLiteral("Starts");
        at.value = QStringLiteral("frame %1 · %2 s").arg(note.start).arg(note.start / 60.0, 0, 'f', 2);
        at.monospace = true;
        sdk::Field length;
        length.key = QStringLiteral("length");
        length.label = QStringLiteral("Length");
        length.value = QStringLiteral("%1 frames").arg(note.length);
        length.monospace = true;
        sdk::Field mml;
        mml.key = QStringLiteral("mml");
        mml.label = QStringLiteral("MML");
        mml.value = QStringLiteral("%1 · \"%2\" at %3")
                        .arg(streamId(music(), note.stream), text.mid(note.position, note.span))
                        .arg(note.position);
        mml.monospace = true;
        inspection.fields = {at, length, mml};
        inspection.note = QStringLiteral("Drag the note up or down to change its pitch: the letter changes in the "
                                         "stream's MML, wrapped in octave commands when it leaves its octave (four "
                                         "more bytes of the shared arena). Every track set and repeat that plays "
                                         "this stream changes with it. Lengths and new notes are edited as text "
                                         "in MML streams.");
        return inspection;
    }
    const QJsonObject set = trackSet();
    inspection.kicker = QStringLiteral("Track set");
    inspection.title = title();
    QStringList choices{QStringLiteral("—")};
    for (const QJsonValue& v : session_->music().value(QStringLiteral("streams")).toArray()) {
        choices << v.toObject().value(QStringLiteral("id")).toString();
    }
    const QJsonArray streams = set.value(QStringLiteral("streams")).toArray();
    for (int c = 0; c < CastleMusic::kChannels; ++c) {
        sdk::Field f;
        f.key = QStringLiteral("stream%1").arg(c);
        f.label = QString::fromLatin1(kChannelLabels[c]);
        f.type = sdk::Field::Type::Choice;
        f.choices = choices;
        f.value = streams.at(c).isString() ? streams.at(c).toString() : QStringLiteral("—");
        inspection.fields << f;
    }
    const QJsonArray parameters = set.value(QStringLiteral("channel_parameters")).toArray();
    for (int c = 0; c < 2; ++c) {
        const int value = parameters.at(c).toInt();
        sdk::Field f;
        f.key = QStringLiteral("parameter%1").arg(c);
        f.label = QStringLiteral("%1 duty/volume").arg(QString::fromLatin1(kChannelLabels[c]));
        f.type = sdk::Field::Type::Integer;
        f.value = value;
        f.minimum = 0;
        f.maximum = 255;
        f.hint = QStringLiteral("duty %1 · vol %2").arg((value >> 6) & 3).arg(value & 15);
        f.monospace = true;
        inspection.fields << f;
    }
    inspection.note = QStringLiteral("A track set starts three MML streams on the APU's pulses and triangle; the "
                                     "pulse bytes are their $4000/$4004 values (duty in the top bits, volume in "
                                     "the low four). Listen renders it with the kit, unsaved edits included.");
    return inspection;
}

// ---------------------------------------------------------------- streams

CastleStreamsModel::CastleStreamsModel(CastleSession* session) : TableModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> CastleStreamsModel::columns() const {
    sdk::TableColumn id;
    id.key = QStringLiteral("id");
    id.label = QStringLiteral("Stream");
    id.width = 110;
    id.monospace = true;
    id.muted = true;
    sdk::TableColumn bytes;
    bytes.key = QStringLiteral("bytes");
    bytes.label = QStringLiteral("Bytes");
    bytes.width = 70;
    bytes.monospace = true;
    bytes.muted = true;
    sdk::TableColumn text;
    text.key = QStringLiteral("text");
    text.label = QStringLiteral("MML");
    text.monospace = true;
    text.editable = true;
    sdk::TableColumn restart;
    restart.key = QStringLiteral("restart");
    restart.label = QStringLiteral("Then");
    restart.width = 120;
    restart.monospace = true;
    restart.editable = true;
    return {id, bytes, text, restart};
}

int CastleStreamsModel::rowCount() const {
    return session_->music().value(QStringLiteral("streams")).toArray().size();
}

QString CastleStreamsModel::cell(int row, int column) const {
    const QJsonObject s = session_->music().value(QStringLiteral("streams")).toArray().at(row).toObject();
    switch (column) {
    case 0: return s.value(QStringLiteral("id")).toString();
    case 1: return QString::number(s.value(QStringLiteral("text")).toString().size() + 3);
    case 2: return s.value(QStringLiteral("text")).toString();
    case 3: return s.value(QStringLiteral("restart")).isString() ? s.value(QStringLiteral("restart")).toString()
                                                                 : QStringLiteral("stop");
    default: return {};
    }
}

bool CastleStreamsModel::isRowDirty(int row) const {
    const QJsonValue now = session_->music().value(QStringLiteral("streams")).toArray().at(row);
    const QJsonValue saved = session_->savedMusic().value(QStringLiteral("streams")).toArray().at(row);
    return now != saved;
}

void CastleStreamsModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool CastleStreamsModel::setCell(int row, int column, const QString& value, QString* error) {
    if (row < 0 || row >= rowCount() || (column != 2 && column != 3)) return TableModel::setCell(row, column, value, error);
    QJsonObject next;
    try {
        if (column == 2) {
            next = CastleMusic::withText(session_->music(), row, value.trimmed());
        } else {
            next = session_->music();
            QJsonArray streams = next.value(QStringLiteral("streams")).toArray();
            QString target = value.trimmed().toLower();
            bool ok = false;
            const int number = target.toInt(&ok);
            if (ok) target = QStringLiteral("stream_%1").arg(number, 2, 10, QLatin1Char('0'));
            QJsonValue restart;
            if (target != QLatin1String("stop") && target != QLatin1String("-") && !target.isEmpty()) {
                bool known = false;
                for (const QJsonValue& v : streams) known = known || v.toObject().value(QStringLiteral("id")).toString() == target;
                if (!known) throw CastleError("Then is stop or a stream, e.g. stream_03.");
                restart = target;
            }
            QJsonObject s = streams.at(row).toObject();
            s.insert(QStringLiteral("restart"), restart);
            streams.replace(row, s);
            next.insert(QStringLiteral("streams"), streams);
        }
    } catch (const CastleError& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
    return session_->editMusic(QStringLiteral("Edit %1").arg(cell(row, 0)), next, error);
}

sdk::Budget CastleStreamsModel::budget() const {
    const QJsonObject& m = session_->music();
    sdk::Budget budget;
    budget.used = CastleMusic::usedBytes(m);
    budget.capacity = m.value(QStringLiteral("capacity_bytes")).toInt();
    budget.text = QStringLiteral("%1 / %2 bytes · %3 free").arg(budget.used).arg(budget.capacity).arg(budget.capacity - budget.used);
    return budget;
}

sdk::Inspection CastleStreamsModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle.mml-music.v1");
    inspection.kicker = QStringLiteral("MML stream");
    inspection.title = cell(row_, 0);
    QStringList users;
    const QString id = cell(row_, 0);
    for (const QJsonValue& v : session_->music().value(QStringLiteral("track_sets")).toArray()) {
        const QJsonObject set = v.toObject();
        const QJsonArray streams = set.value(QStringLiteral("streams")).toArray();
        for (int c = 0; c < streams.size(); ++c) {
            if (streams.at(c).toString() == id) {
                users << QStringLiteral("%1 (%2)").arg(set.value(QStringLiteral("name")).toString(),
                                                      QString::fromLatin1(kChannelLabels[c]));
            }
        }
    }
    sdk::Field used;
    used.key = QStringLiteral("used");
    used.label = QStringLiteral("Played by");
    used.value = users.isEmpty() ? QStringLiteral("restarts only") : users.join(QStringLiteral(", "));
    inspection.fields = {used};
    inspection.note = QStringLiteral("Notes are c d e f g a b, + or - after a note for sharp or flat, r for a "
                                     "rest; a length (@ 8 4 2 1, shortest first) and a dot may follow. o0-o9 sets "
                                     "the octave, l the default length, t0-t9 the tempo of all three channels, v a "
                                     "volume the engine skips. Then names the stream played next, or stop. All 21 "
                                     "streams share the arena; the budget shows what is left.");
    return inspection;
}

}  // namespace rl::castle
