#include "flicky/plugin/sound_models.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "sdk/plugin.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace rl::flicky {

namespace {

QString hex(int value, int digits) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

const QColor kChannelColors[] = {QColor(0x94, 0xbc, 0xe3), QColor(0xe3, 0xb5, 0x94), QColor(0xa8, 0xd8, 0x8f),
                                 QColor(0xd8, 0x9a, 0xd8), QColor(0xe8, 0xd5, 0x7a), QColor(0x8f, 0xd8, 0xcf),
                                 QColor(0xd8, 0x8f, 0x8f), QColor(0xb0, 0xb0, 0xb8), QColor(0x9a, 0xa8, 0xe8)};

QString parseOffset(const QString& id, int* channel) {
    const int colon = id.indexOf(QLatin1Char(':'));
    *channel = id.left(colon).toInt();
    return id.mid(colon + 1);
}

}  // namespace

// ---------------------------------------------------------------- header

SoundHeaderModel::SoundHeaderModel(FlickySession* session, const QString& id)
    : SequencerModel(session), session_(session), id_(id) {
    connect(session_, &FlickySession::soundChanged, this, &sdk::EditorModel::contentChanged);
    // Open on the first channel that plays anything; some tracks are empty.
    const FlickySoundHeader h = header();
    for (int i = 0; i < h.tracks.size(); ++i) {
        const QVector<FlickySoundNote>& notes = sequenceNotes(h.tracks.at(i).sequence);
        if (std::any_of(notes.begin(), notes.end(), [](const FlickySoundNote& n) { return n.note >= 0; })) {
            channel_ = i;
            break;
        }
    }
}

FlickySoundHeader SoundHeaderModel::header() const {
    return session_->sound().header(id_);
}

const QVector<FlickySoundNote>& SoundHeaderModel::sequenceNotes(const QString& sequence) const {
    if (notesRevision_ != session_->soundRevision()) {
        notes_.clear();
        notesRevision_ = session_->soundRevision();
    }
    auto it = notes_.find(sequence);
    if (it == notes_.end()) {
        QVector<FlickySoundNote> notes;
        try {
            notes = session_->sound().notes(sequence);
        } catch (const std::exception&) {
        }
        it = notes_.insert(sequence, notes);
    }
    return *it;
}

QVector<sdk::SequencerChannel> SoundHeaderModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const FlickySoundHeader h = header();
    const QSet<int>& muted = session_->mutedChannels(id_);
    QHash<QString, int> counts;
    for (int i = 0; i < h.tracks.size(); ++i) {
        const FlickySoundTrack& track = h.tracks.at(i);
        sdk::SequencerChannel channel;
        channel.id = QString::number(i);
        const QString type = track.type.toUpper();
        channel.label = QStringLiteral("%1 %2").arg(type).arg(++counts[type]);
        channel.detail = track.sequence;
        channel.color = kChannelColors[i % 9];
        channel.muted = muted.contains(i);
        result.append(channel);
    }
    return result;
}

void SoundHeaderModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < header().tracks.size()) {
        channel_ = channel;
        selected_.clear();
        emit selectionChanged();
        emit contentChanged();
    }
}

void SoundHeaderModel::setChannelMuted(int channel, bool muted) {
    QSet<int>& set = session_->mutedChannels(id_);
    if (muted) {
        set.insert(channel);
    } else {
        set.remove(channel);
    }
    emit contentChanged();
}

QVector<sdk::SequencerNote> SoundHeaderModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    const FlickySoundHeader h = header();
    if (channel < 0 || channel >= h.tracks.size()) {
        return result;
    }
    for (const FlickySoundNote& note : sequenceNotes(h.tracks.at(channel).sequence)) {
        sdk::SequencerNote n;
        n.id = QStringLiteral("%1:%2").arg(channel).arg(note.offset);
        n.start = note.start;
        n.length = std::max(1, note.duration);
        n.pitch = note.note;
        n.lengthEditable = note.durationOffset >= 0;
        n.pitchEditable = note.note >= 0;
        result.append(n);
    }
    return result;
}

bool SoundHeaderModel::findNote(const QString& id, int* channel, FlickySoundNote* note) const {
    const int offset = parseOffset(id, channel).toInt();
    const FlickySoundHeader h = header();
    if (*channel < 0 || *channel >= h.tracks.size()) {
        return false;
    }
    for (const FlickySoundNote& n : sequenceNotes(h.tracks.at(*channel).sequence)) {
        if (n.offset == offset) {
            *note = n;
            return true;
        }
    }
    return false;
}

void SoundHeaderModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void SoundHeaderModel::beginEdit(const QString& id) {
    selectNote(id);
    before_ = session_->sound();
    beforeRevision_ = session_->soundRevision();
    changed_ = false;
}

bool SoundHeaderModel::editNote(const QString& id, int pitch, int length) {
    int channel = 0;
    FlickySoundNote note;
    if (!before_ || !findNote(id, &channel, &note)) {
        return false;
    }
    const QString sequence = header().tracks.at(channel).sequence;
    try {
        bool touched = false;
        if (note.note >= 0 && pitch >= 0 && pitch != note.note) {
            session_->soundLive().setSequenceByte(sequence, note.offset, 0x81 + std::clamp(pitch, 0, 94));
            touched = true;
        }
        if (note.durationOffset >= 0 && length != note.duration) {
            session_->soundLive().setSequenceByte(sequence, note.durationOffset, std::clamp(length, 1, 127));
            touched = true;
        }
        if (!touched) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    changed_ = true;
    session_->touchSound();
    return true;
}

void SoundHeaderModel::endEdit() {
    if (before_ && changed_) {
        session_->commitSound(*before_, beforeRevision_, tr("Edit note in %1").arg(id_));
    }
    before_.reset();
    changed_ = false;
}

void SoundHeaderModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) {
        return;
    }
    QStringList arguments{QStringLiteral("studio"), QStringLiteral("flicky-sound-preview"), session_->projectPath(),
                          QStringLiteral("--header"), id_, QStringLiteral("--seconds"), QStringLiteral("30")};
    const QSet<int>& muted = session_->mutedChannels(id_);
    if (!muted.isEmpty()) {
        for (int i = 0; i < header().tracks.size(); ++i) {
            if (!muted.contains(i)) {
                arguments << QStringLiteral("--channel") << QString::number(i);
            }
        }
    }
    // Unsaved edits are rendered from a staged copy of the source.
    if (session_->isSoundDirty()) {
        const QString source = session_->sound().source();
        const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
        QDir().mkpath(directory);
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        const QString path = QDir(directory).filePath(QStringLiteral(".flicky-unsaved-%1.asm").arg(hash));
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(source.toUtf8());
            file.commit();
        }
        arguments << QStringLiteral("--document-override") << path;
    }
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
    spec.label = tr("Render %1").arg(id_);
    const QString projectDirectory = session_->projectDirectory();
    const QString id = id_;
    // The model may close before rendering ends; capture values only.
    host->runProcess(spec, [host, projectDirectory, id](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                sdk::Diagnostic d;
                d.severity = sdk::Severity::Error;
                d.source = QStringLiteral("sound");
                d.message = tr("Sound preview failed: %1")
                                .arg(QString::fromUtf8(result.standardError).trimmed().section(QLatin1Char('\n'), -1));
                host->report(d);
            }
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(result.standardOutput).object();
        QString wav = json.value(QStringLiteral("wav")).toObject().value(QStringLiteral("path")).toString();
        if (QFileInfo(wav).isRelative()) {
            wav = QDir(projectDirectory).filePath(wav);
        }
        if (!QFileInfo(wav).isFile()) {
            sdk::Diagnostic d;
            d.severity = sdk::Severity::Error;
            d.source = QStringLiteral("sound");
            d.message = tr("The kit reported no WAV for %1.").arg(id);
            host->report(d);
            return;
        }
        host->log(tr("  Playing %1").arg(QDir::toNativeSeparators(wav)));
        host->playAudio(wav);
    });
}

void SoundHeaderModel::stopPreview() {
    if (sdk::Host* host = session_->host()) {
        host->stopAudio();
    }
}

bool SoundHeaderModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void SoundHeaderModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) {
        host->setAudioLoop(loop);
    }
}

sdk::Inspection SoundHeaderModel::inspect() const {
    const FlickySoundHeader h = header();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("flicky.sound.v1");
    int channel = 0;
    FlickySoundNote note;
    if (!selected_.isEmpty() && findNote(selected_, &channel, &note)) {
        const QVector<sdk::SequencerChannel> chans = channels();
        inspection.kicker = tr("Note · %1").arg(chans.value(channel).label);
        inspection.title = pitchName(note.note);
        sdk::Field start;
        start.key = QStringLiteral("start");
        start.label = tr("Start");
        start.value = note.start;
        start.hint = QStringLiteral("ticks");
        start.monospace = true;
        sdk::Field length;
        length.key = QStringLiteral("length");
        length.label = tr("Length");
        length.type = note.durationOffset >= 0 ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
        length.minimum = 1;
        length.maximum = 127;
        length.value = note.duration;
        length.hint = note.sharedDuration ? QStringLiteral("shared") : QStringLiteral("ticks");
        length.monospace = true;
        sdk::Field pitch;
        pitch.key = QStringLiteral("pitch");
        pitch.label = tr("Pitch");
        pitch.type = note.note >= 0 ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
        pitch.minimum = 0;
        pitch.maximum = 94;
        pitch.value = note.note >= 0 ? QVariant(note.note) : QVariant(QStringLiteral("rest"));
        pitch.hint = QStringLiteral("0–94");
        pitch.monospace = true;
        inspection.fields = {start, length, pitch};
        inspection.note = note.durationOffset < 0
                              ? tr("This note reuses the previous duration; change the note that sets it.")
                              : note.sharedDuration
                                    ? tr("Later notes reuse this duration byte, so they move with it.")
                                    : tr("Drag up or down for pitch, Shift-drag sideways for length.");
        const QString sequence = h.tracks.value(channel).sequence;
        inspection.engine = {{tr("Sequence"), sequence},
                             {tr("Note byte"), tr("%1 at +%2")
                                                               .arg(hex(note.note >= 0 ? 0x81 + note.note : 0x80, 2))
                                                               .arg(note.offset)},
                             {tr("Duration"), note.durationOffset >= 0
                                                              ? QStringLiteral("+%1").arg(note.durationOffset)
                                                              : QStringLiteral("inherited")}};
        return inspection;
    }
    inspection.kicker = h.kind.isEmpty() ? tr("Sound") : h.kind.toUpper();
    inspection.title = id_;
    sdk::Field voices;
    voices.key = QStringLiteral("voices");
    voices.label = tr("FM voices");
    voices.type = sdk::Field::Type::Choice;
    voices.choices = session_->sound().voiceBankIds();
    voices.value = h.voices;
    sdk::Field scale;
    scale.key = QStringLiteral("scale");
    scale.label = tr("Duration scale");
    scale.type = sdk::Field::Type::Integer;
    scale.minimum = 0;
    scale.maximum = 255;
    scale.value = h.scale;
    scale.monospace = true;
    inspection.fields = {voices, scale};
    if (h.kind == QLatin1String("music")) {
        sdk::Field tempo = scale;
        tempo.key = QStringLiteral("tempo");
        tempo.label = tr("Tempo");
        tempo.value = h.tempo;
        inspection.fields << tempo;
    }
    if (channel_ < h.tracks.size()) {
        const FlickySoundTrack& track = h.tracks.at(channel_);
        sdk::Field transpose;
        transpose.key = QStringLiteral("transpose");
        transpose.label = tr("Transpose");
        transpose.type = sdk::Field::Type::Integer;
        transpose.minimum = 0;
        transpose.maximum = 255;
        transpose.value = track.transpose;
        transpose.hint = tr("raw byte");
        transpose.monospace = true;
        sdk::Field volume = transpose;
        volume.key = QStringLiteral("volume");
        volume.label = tr("Volume");
        volume.value = track.volume;
        inspection.fields << transpose << volume;
    }
    inspection.note = tr("Transpose and volume belong to the selected channel. Listen renders "
                         "the unmuted channels with the source's YMFM renderer, including unsaved "
                         "edits.");
    inspection.engine = {{tr("Header"), id_},
                         {tr("Tracks"), QString::number(h.tracks.size())},
                         {tr("Source"), QStringLiteral("sound/z80_sound_data.asm")}};
    return inspection;
}

bool SoundHeaderModel::setField(const QString& key, const QVariant& value, QString* error) {
    const QString id = id_;
    int channel = 0;
    FlickySoundNote note;
    if ((key == QLatin1String("length") || key == QLatin1String("pitch")) && findNote(selected_, &channel, &note)) {
        beginEdit(selected_);
        const bool ok = key == QLatin1String("pitch") ? editNote(selected_, value.toInt(), note.duration)
                                                      : editNote(selected_, note.note, value.toInt());
        endEdit();
        return ok;
    }
    if (key == QLatin1String("voices")) {
        const QString bank = value.toString();
        return session_->editSound(tr("Set %1 voices").arg(id),
                                   [id, bank](FlickySoundDocument& d) { d.setHeaderVoices(id, bank); }, error);
    }
    if (key == QLatin1String("scale") || key == QLatin1String("tempo")) {
        const int v = value.toInt();
        return session_->editSound(tr("Edit %1 %2").arg(id, key),
                                   [id, key, v](FlickySoundDocument& d) { d.setHeaderValue(id, key, v); }, error);
    }
    if (key == QLatin1String("transpose") || key == QLatin1String("volume")) {
        const int track = channel_;
        const int v = value.toInt();
        return session_->editSound(tr("Edit %1 track %2 %3").arg(id).arg(track).arg(key),
                                   [id, track, key, v](FlickySoundDocument& d) { d.setTrackValue(id, track, key, v); },
                                   error);
    }
    return SequencerModel::setField(key, value, error);
}

QString SoundHeaderModel::statusText() const {
    int channel = 0;
    FlickySoundNote note;
    if (!selected_.isEmpty() && findNote(selected_, &channel, &note)) {
        return tr("%1 · %2 at tick %3, %4 ticks")
            .arg(id_, pitchName(note.note))
            .arg(note.start)
            .arg(note.duration);
    }
    return tr("%1 · %2 tracks").arg(id_).arg(header().tracks.size());
}

// ---------------------------------------------------------------- FM voices

namespace {

struct Parameter {
    const char* label;
    int maximum;
};

// Columns after Voice and Op: per-voice Alg/FB, then per-operator values.
const Parameter kVoiceParameters[] = {{"Alg", 7}, {"FB", 7}};
const Parameter kOperatorParameters[] = {{"DT", 7},   {"MUL", 15}, {"RS", 3},  {"AR", 31}, {"AM", 1},
                                         {"D1R", 31}, {"D2R", 31}, {"SL", 15}, {"RR", 15}, {"TL", 127}};

int operatorValue(const FlickyFmOperator& op, int index) {
    switch (index) {
    case 0: return op.detune;
    case 1: return op.multiple;
    case 2: return op.rateScale;
    case 3: return op.attack;
    case 4: return op.amplitudeModulation;
    case 5: return op.decay1;
    case 6: return op.decay2;
    case 7: return op.sustain;
    case 8: return op.release;
    default: return op.totalLevel;
    }
}

void setOperatorValue(FlickyFmOperator& op, int index, int value) {
    switch (index) {
    case 0: op.detune = value; break;
    case 1: op.multiple = value; break;
    case 2: op.rateScale = value; break;
    case 3: op.attack = value; break;
    case 4: op.amplitudeModulation = value; break;
    case 5: op.decay1 = value; break;
    case 6: op.decay2 = value; break;
    case 7: op.sustain = value; break;
    case 8: op.release = value; break;
    default: op.totalLevel = value; break;
    }
}

bool sameVoice(const FlickyFmVoice& a, const FlickyFmVoice& b) {
    if (a.algorithm != b.algorithm || a.feedback != b.feedback) return false;
    for (int o = 0; o < 4; ++o) {
        for (int p = 0; p < 10; ++p) {
            if (operatorValue(a.operators[o], p) != operatorValue(b.operators[o], p)) return false;
        }
    }
    return true;
}

}  // namespace

FmVoiceModel::FmVoiceModel(FlickySession* session, const QString& bank)
    : TableModel(session), session_(session), bank_(bank) {
    connect(session_, &FlickySession::soundChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> FmVoiceModel::columns() const {
    QVector<sdk::TableColumn> columns;
    sdk::TableColumn voice;
    voice.key = QStringLiteral("voice");
    voice.label = tr("Voice");
    voice.width = 80;
    voice.muted = true;
    sdk::TableColumn op = voice;
    op.key = QStringLiteral("op");
    op.label = tr("Op");
    op.width = 60;
    columns << voice << op;
    for (const Parameter& p : kVoiceParameters) {
        sdk::TableColumn c;
        c.key = QString::fromLatin1(p.label);
        c.label = c.key;
        c.width = 64;
        c.monospace = true;
        c.editable = true;
        columns << c;
    }
    for (const Parameter& p : kOperatorParameters) {
        sdk::TableColumn c;
        c.key = QString::fromLatin1(p.label);
        c.label = c.key;
        c.width = 0;
        c.monospace = true;
        c.editable = true;
        columns << c;
    }
    return columns;
}

int FmVoiceModel::rowCount() const {
    return session_->sound().voiceCount(bank_) * 4;
}

QString FmVoiceModel::cell(int row, int column) const {
    const int voiceIndex = row / 4;
    const int op = row % 4;
    const FlickyFmVoice voice = session_->sound().voice(bank_, voiceIndex);
    if (column == 0) return op == 0 ? QString::number(voiceIndex) : QString();
    if (column == 1) return QString::number(op + 1);
    if (column == 2) return op == 0 ? QString::number(voice.algorithm) : QString();
    if (column == 3) return op == 0 ? QString::number(voice.feedback) : QString();
    return QString::number(operatorValue(voice.operators[op], column - 4));
}

bool FmVoiceModel::isRowDirty(int row) const {
    return !sameVoice(session_->sound().voice(bank_, row / 4), session_->savedSound().voice(bank_, row / 4));
}

void FmVoiceModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool FmVoiceModel::setCell(int row, int column, const QString& value, QString* error) {
    const int voiceIndex = row / 4;
    const int op = row % 4;
    bool ok = false;
    const int number = value.trimmed().toInt(&ok);
    const int maximum = column < 4 ? kVoiceParameters[column - 2].maximum : kOperatorParameters[column - 4].maximum;
    if (column < 2 || (column < 4 && op != 0)) {
        return TableModel::setCell(row, column, value, error);
    }
    if (!ok || number < 0 || number > maximum) {
        if (error) *error = tr("Use 0–%1.").arg(maximum);
        return false;
    }
    FlickyFmVoice voice = session_->sound().voice(bank_, voiceIndex);
    if (column == 2) voice.algorithm = number;
    else if (column == 3) voice.feedback = number;
    else setOperatorValue(voice.operators[op], column - 4, number);
    const QString bank = bank_;
    return session_->editSound(tr("Edit %1 voice %2").arg(bank).arg(voiceIndex),
                               [bank, voiceIndex, voice](FlickySoundDocument& d) { d.setVoice(bank, voiceIndex, voice); },
                               error);
}

sdk::Inspection FmVoiceModel::inspect() const {
    const int voiceIndex = row_ / 4;
    const FlickyFmVoice voice = session_->sound().voice(bank_, voiceIndex);
    sdk::Inspection inspection;
    inspection.kicker = tr("FM voice");
    inspection.title = tr("%1 · voice %2").arg(bank_).arg(voiceIndex);
    sdk::Field alg;
    alg.key = QStringLiteral("alg");
    alg.label = tr("Algorithm");
    alg.value = voice.algorithm;
    alg.monospace = true;
    sdk::Field fb = alg;
    fb.key = QStringLiteral("fb");
    fb.label = tr("Feedback");
    fb.value = voice.feedback;
    inspection.fields = {alg, fb};
    inspection.note = tr("YM2612 operator values, edited in the table. TL is the operator's "
                         "attenuation: carriers set the voice's loudness.");
    inspection.engineTag = QStringLiteral("flicky.sound.v1");
    inspection.engine = {{tr("Bank"), bank_},
                         {tr("Voices"), QString::number(session_->sound().voiceCount(bank_))},
                         {tr("Encoding"), tr("25 bytes per voice")}};
    return inspection;
}

QString FmVoiceModel::statusText() const {
    return tr("%1 · voice %2 · operator %3").arg(bank_).arg(row_ / 4).arg(row_ % 4 + 1);
}

// ---------------------------------------------------------------- envelopes

EnvelopeModel::EnvelopeModel(FlickySession* session) : TableModel(session), session_(session) {
    connect(session_, &FlickySession::soundChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> EnvelopeModel::columns() const {
    sdk::TableColumn index;
    index.key = QStringLiteral("index");
    index.label = tr("Byte");
    index.width = 90;
    index.muted = true;
    sdk::TableColumn value;
    value.key = QStringLiteral("value");
    value.label = tr("Value");
    value.width = 140;
    value.monospace = true;
    value.editable = true;
    sdk::TableColumn decimal;
    decimal.key = QStringLiteral("decimal");
    decimal.label = tr("Decimal");
    decimal.muted = true;
    decimal.monospace = true;
    return {index, value, decimal};
}

int EnvelopeModel::rowCount() const {
    return session_->sound().priorityEnvelopeBytes().size();
}

QString EnvelopeModel::cell(int row, int column) const {
    const int value = session_->sound().priorityEnvelopeBytes().value(row);
    if (column == 0) return QString::number(row);
    if (column == 1) return hex(value, 2);
    return QString::number(value);
}

bool EnvelopeModel::isRowDirty(int row) const {
    return session_->sound().priorityEnvelopeBytes().value(row) !=
           session_->savedSound().priorityEnvelopeBytes().value(row);
}

void EnvelopeModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool EnvelopeModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) {
        return TableModel::setCell(row, column, value, error);
    }
    QString digits = value.trimmed();
    digits.remove(QLatin1Char('$'));
    bool ok = false;
    const int byte = digits.toInt(&ok, 16);
    if (!ok || byte < 0 || byte > 0xFF) {
        if (error) *error = tr("A byte is up to two hex digits.");
        return false;
    }
    return session_->editSound(tr("Edit envelope byte %1").arg(row),
                               [row, byte](FlickySoundDocument& d) { d.setPriorityEnvelopeByte(row, byte); }, error);
}

sdk::Inspection EnvelopeModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Sound data");
    inspection.title = tr("Byte %1").arg(row_);
    sdk::Field value;
    value.key = QStringLiteral("value");
    value.label = tr("Value");
    value.value = hex(session_->sound().priorityEnvelopeBytes().value(row_), 2);
    value.monospace = true;
    inspection.fields = {value};
    inspection.note = tr("Sound-effect priorities and PSG envelopes of the Z80 driver "
                         "(zPriorityAndEnvelopeData).");
    inspection.engineTag = QStringLiteral("flicky.sound.v1");
    inspection.engine = {{tr("Block"), QStringLiteral("zPriorityAndEnvelopeData")},
                         {tr("Saved"), hex(session_->savedSound().priorityEnvelopeBytes().value(row_), 2)}};
    return inspection;
}

QString EnvelopeModel::statusText() const {
    return tr("Priority & envelopes · byte %1").arg(row_);
}

}  // namespace rl::flicky
