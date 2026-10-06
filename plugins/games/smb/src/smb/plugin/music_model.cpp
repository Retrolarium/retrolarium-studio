#include "smb/plugin/music_model.hpp"

#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <map>

namespace rl::smb {

namespace {

constexpr double kFrameRate = 60.0988;

QString hex(int value, int digits = 2) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

const QStringList& beatNames() {
    static const QStringList names{rlTr("smb", "Rest"), rlTr("smb", "Short"), rlTr("smb", "Strong"),
                                   rlTr("smb", "Long")};
    return names;
}

QString role(const QString& channel) {
    if (channel == QLatin1String("square2")) return QStringLiteral("melody");
    if (channel == QLatin1String("square1")) return QStringLiteral("harmony");
    if (channel == QLatin1String("triangle")) return QStringLiteral("bass");
    return QStringLiteral("drums");
}

QColor channelColor(const QString& channel) {
    if (channel == QLatin1String("square2")) return QColor(0x7a, 0xb8, 0xff);
    if (channel == QLatin1String("square1")) return QColor(0xff, 0xa8, 0x4c);
    if (channel == QLatin1String("triangle")) return QColor(0x6d, 0xd4, 0x8e);
    return QColor(0xd4, 0x8a, 0xff);
}

bool sounds(SmbMusicEvent::Kind kind) {
    return kind == SmbMusicEvent::Kind::Note || kind == SmbMusicEvent::Kind::Rest || kind == SmbMusicEvent::Kind::Beat;
}

// The length-table entry a length choice names: its last number, wherever a
// translation puts the words around it.
int entryOf(const QString& choice) {
    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\D*$")).match(choice);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}
}  // namespace

SmbMusicModel::SmbMusicModel(SmbSession* session, const QString& header)
    : SequencerModel(session), session_(session), header_(header) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

const SmbMusic& SmbMusicModel::music() const {
    if (revision_ != session_->revision()) {
        music_ = SmbMusic::fromJson(session_->document().resource(session_->music()));
        try {
            timeline_ = music_.timeline(header_);
            timelineError_.clear();
        } catch (const SmbError& e) {
            timeline_ = SmbMusicTimeline();
            timelineError_ = QString::fromUtf8(e.what());
        }
        revision_ = session_->revision();
    }
    return music_;
}

const SmbMusicTimeline& SmbMusicModel::timeline() const {
    music();
    return timeline_;
}

QVector<sdk::SequencerChannel> SmbMusicModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    for (const SmbMusicChannel& c : timeline().channels) {
        sdk::SequencerChannel channel;
        channel.id = c.id;
        channel.label = SmbMusic::channelLabel(c.id);
        channel.detail = role(c.id);
        channel.color = channelColor(c.id);
        channel.muted = muted_.contains(c.id);
        result << channel;
    }
    return result;
}

void SmbMusicModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < timeline().channels.size()) {
        channel_ = channel;
        emit selectionChanged();
        emit contentChanged();
    }
}

void SmbMusicModel::setChannelMuted(int channel, bool muted) {
    if (channel < 0 || channel >= timeline().channels.size()) return;
    const QString id = timeline().channels.at(channel).id;
    if (muted) muted_.insert(id); else muted_.remove(id);
    emit contentChanged();
}

int SmbMusicModel::beatPitch(int beat) const {
    // Drums sit above the tonal range: long, strong, short upwards.
    static constexpr int kBeat[4] = {-1, 98, 94, 90};
    return kBeat[beat & 3];
}

QVector<sdk::SequencerNote> SmbMusicModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    if (channel < 0 || channel >= timeline().channels.size()) return result;
    const SmbMusicChannel& c = timeline().channels.at(channel);
    for (int i = 0; i < c.events.size(); ++i) {
        const SmbMusicEvent& e = c.events.at(i);
        if (!sounds(e.kind)) continue;
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(c.id).arg(i);
        note.start = e.tick;
        note.length = std::max(1, e.duration);
        if (e.kind == SmbMusicEvent::Kind::Beat) {
            note.pitch = beatPitch(e.beat);
            note.pitchEditable = false;
            note.lengthEditable = true;
        } else if (e.kind == SmbMusicEvent::Kind::Rest) {
            note.pitch = -1;
            note.pitchEditable = false;
        } else {
            note.pitch = music().pitchOf(e.periodIndex, c.id);
            note.pitchEditable = note.pitch >= 0;
            note.lengthEditable = c.id == QLatin1String("square1");
        }
        result << note;
    }
    return result;
}

int SmbMusicModel::pitchMinimum() const {
    int low = 60;
    for (int c = 0; c < timeline().channels.size(); ++c) {
        for (const sdk::SequencerNote& n : notes(c)) {
            if (n.pitch >= 0) low = std::min(low, n.pitch);
        }
    }
    return std::max(0, low - 3);
}

int SmbMusicModel::pitchMaximum() const {
    int high = 72;
    for (int c = 0; c < timeline().channels.size(); ++c) {
        for (const sdk::SequencerNote& n : notes(c)) {
            high = std::max(high, n.pitch);
        }
    }
    return std::min(127, high + 3);
}

int SmbMusicModel::ticksPerBar() const {
    // Four of the melody's most common note length.
    std::map<int, int> counts;
    if (!timeline().channels.isEmpty()) {
        for (const SmbMusicEvent& e : timeline().channels.first().events) {
            if (e.kind == SmbMusicEvent::Kind::Note) ++counts[e.duration];
        }
    }
    int best = 0;
    int bestCount = 0;
    for (const auto& [duration, count] : counts) {
        if (count > bestCount) {
            best = duration;
            bestCount = count;
        }
    }
    return best > 0 ? best * 4 : 32;
}

bool SmbMusicModel::find(const QString& id, int* channel, int* event) const {
    const QString channelId = id.section(QLatin1Char(':'), 0, 0);
    bool ok = false;
    const int index = id.section(QLatin1Char(':'), 1).toInt(&ok);
    for (int c = 0; c < timeline().channels.size(); ++c) {
        if (timeline().channels.at(c).id == channelId && ok && index >= 0 &&
            index < timeline().channels.at(c).events.size()) {
            *channel = c;
            *event = index;
            return true;
        }
    }
    return false;
}

void SmbMusicModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        int c = 0;
        int e = 0;
        if (find(id, &c, &e) && c != channel_) {
            channel_ = c;
        }
        emit selectionChanged();
    }
}

int SmbMusicModel::lengthIndexFor(int duration) const {
    const int base = timeline().lengthBase;
    int best = -1;
    for (int i = 0; i < 8 && base + i < music().lengths.size(); ++i) {
        if (music().lengths.at(base + i) == duration) return i;
        if (best < 0 || std::abs(music().lengths.at(base + i) - duration) <
                            std::abs(music().lengths.at(base + best) - duration)) {
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------- editing

void SmbMusicModel::beginEdit(const QString& id) {
    selectNote(id);
    before_ = music();
    changed_ = false;
}

bool SmbMusicModel::editNote(const QString& id, int pitch, int length) {
    int c = 0;
    int e = 0;
    if (!before_ || !find(id, &c, &e)) return false;
    // From the state the drag started in.
    SmbMusic next = *before_;
    const SmbMusicTimeline start = next.timeline(header_);
    if (c >= start.channels.size() || e >= start.channels.at(c).events.size()) return false;
    const SmbMusicChannel& channel = start.channels.at(c);
    const SmbMusicEvent& event = channel.events.at(e);
    try {
        if (event.kind == SmbMusicEvent::Kind::Note && pitch >= 0 && pitch != next.pitchOf(event.periodIndex, channel.id)) {
            const int period = next.periodFor(pitch, channel.id);
            if (period < 0) return false;   // no period entry sounds that pitch
            next.setNotePeriod(channel.id, event.offset, period);
        }
        if ((channel.id == QLatin1String("square1") || channel.id == QLatin1String("noise")) && length != event.duration) {
            const int index = lengthIndexFor(length);
            if (index >= 0 && index != event.lengthIndex) next.setEventLength(channel.id, event.offset, index);
        }
    } catch (const SmbError&) {
        return false;
    }
    if (next.arena == music().arena) return false;
    // Shown from the cache; the document is written once, on release.
    music_ = next;
    try {
        timeline_ = music_.timeline(header_);
        timelineError_.clear();
    } catch (const SmbError& e) {
        timeline_ = SmbMusicTimeline();
        timelineError_ = QString::fromUtf8(e.what());
    }
    changed_ = true;
    emit contentChanged();
    return true;
}

void SmbMusicModel::endEdit() {
    const bool changed = before_ && changed_ && music_.arena != before_->arena;
    const SmbMusic next = music_;
    before_.reset();
    changed_ = false;
    revision_ = ~quint64(0);  // read the document again
    if (changed) {
        const QString id = session_->music();
        session_->edit(
            tr("Edit a note of %1").arg(header_),
            [id, next](SmbResourceDocument& d) { d.setResource(id, next.toJson(d.resource(id))); }, nullptr);
    }
    emit contentChanged();
}

bool SmbMusicModel::editMusic(const QString& text, const std::function<void(SmbMusic&)>& change, QString* error) {
    const QString id = session_->music();
    return session_->edit(
        text,
        [id, change](SmbResourceDocument& d) {
            const QJsonObject json = d.resource(id);
            SmbMusic next = SmbMusic::fromJson(json);
            change(next);
            d.setResource(id, next.toJson(json));
        },
        error);
}

bool SmbMusicModel::setField(const QString& key, const QVariant& value, QString* error) {
    int c = 0;
    int e = 0;
    if (!find(selected_, &c, &e)) {
        return SequencerModel::setField(key, value, error);
    }
    const SmbMusicChannel channel = timeline().channels.at(c);
    const SmbMusicEvent event = channel.events.at(e);
    const QString channelId = channel.id;
    const int offset = event.offset;
    if (key == QLatin1String("period")) {
        const int period = value.toInt();
        return editMusic(tr("Set a note of %1").arg(header_),
                         [channelId, offset, period](SmbMusic& m) { m.setNotePeriod(channelId, offset, period); }, error);
    }
    if (key == QLatin1String("length")) {
        // "20 ticks (entry 2)", in any language: the entry is the last number.
        const int index = entryOf(value.toString());
        return editMusic(tr("Set a note length of %1").arg(header_),
                         [channelId, offset, index](SmbMusic& m) { m.setEventLength(channelId, offset, index); }, error);
    }
    if (key == QLatin1String("run")) {
        // The length byte before this note on square 2 or the triangle.
        int lengthOffset = -1;
        for (int i = e; i >= 0; --i) {
            if (channel.events.at(i).kind == SmbMusicEvent::Kind::Length) {
                lengthOffset = channel.events.at(i).offset;
                break;
            }
        }
        if (lengthOffset < 0) {
            if (error) *error = tr("No length byte starts this run.");
            return false;
        }
        const int index = entryOf(value.toString());
        return editMusic(tr("Set a run length of %1").arg(header_),
                         [channelId, lengthOffset, index](SmbMusic& m) { m.setEventLength(channelId, lengthOffset, index); },
                         error);
    }
    if (key == QLatin1String("beat")) {
        const int beat = std::max(0, beatNames().indexOf(value.toString()));
        return editMusic(tr("Set a beat of %1").arg(header_),
                         [offset, beat](SmbMusic& m) { m.setBeat(offset, beat); }, error);
    }
    return SequencerModel::setField(key, value, error);
}

// ---------------------------------------------------------------- inspector

QString SmbMusicModel::selectorOf() const {
    for (const auto& selector : music().selectors) {
        if (selector.second == header_) return selector.first;
    }
    return {};
}

sdk::Inspection SmbMusicModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("smb.apu-music.v1");
    const SmbMusicTimeline& t = timeline();
    const int base = t.lengthBase;
    QStringList lengthChoices;
    for (int i = 0; i < 8 && base + i < music().lengths.size(); ++i) {
        lengthChoices << tr("%1 ticks (entry %2)").arg(music().lengths.at(base + i)).arg(i);
    }
    int c = 0;
    int e = 0;
    if (!find(selected_, &c, &e)) {
        inspection.kicker = tr("Song");
        inspection.title = header_;
        sdk::Field length;
        length.key = QStringLiteral("duration");
        length.label = tr("Length");
        length.value = tr("%1 ticks · %2 s").arg(t.duration).arg(t.duration / kFrameRate, 0, 'f', 1);
        length.monospace = true;
        sdk::Field chans;
        chans.key = QStringLiteral("channels");
        chans.label = tr("Channels");
        QStringList names;
        for (const SmbMusicChannel& ch : t.channels) names << SmbMusic::channelLabel(ch.id);
        chans.value = names.join(QStringLiteral(", "));
        sdk::Field used;
        used.key = QStringLiteral("used");
        used.label = tr("Played as");
        QStringList selectors;
        for (const auto& s : music().selectors) {
            if (s.second == header_) selectors << s.first;
        }
        if (music().groundSchedule.contains(header_)) selectors << tr("ground theme");
        used.value = selectors.isEmpty() ? QStringLiteral("—") : selectors.join(QStringLiteral(", "));
        inspection.fields = {length, chans, used};
        inspection.note = timelineError_.isEmpty()
                              ? tr("Drag a note up or down to change its pitch; square 1 and drum notes "
                                   "also change length. Songs share stream bytes, so the inspector names "
                                   "every song a note belongs to. Play renders the song with the kit.")
                              : timelineError_;
        inspection.engine = {{tr("Stream"), hex(t.channels.isEmpty() ? 0 : t.channels.first().entry, 4)},
                             {tr("Length row"), QString::number(base)}};
        return inspection;
    }
    const SmbMusicChannel& channel = t.channels.at(c);
    const SmbMusicEvent& event = channel.events.at(e);
    const int raw = music().arena.value(event.offset);
    inspection.kicker = tr("Note · %1").arg(SmbMusic::channelLabel(channel.id));
    sdk::Field start;
    start.key = QStringLiteral("start");
    start.label = tr("Start");
    start.value = tr("tick %1").arg(event.tick);
    start.monospace = true;
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    if (channel.id == QLatin1String("square1") || channel.id == QLatin1String("noise")) {
        length.type = sdk::Field::Type::Choice;
        length.choices = lengthChoices;
        length.value = lengthChoices.value(event.lengthIndex);
    } else {
        length.value = tr("%n ticks", nullptr, event.duration);
        length.monospace = true;
    }
    inspection.fields << start;
    if (event.kind == SmbMusicEvent::Kind::Beat) {
        inspection.title = beatNames().value(event.beat);
        sdk::Field beat;
        beat.key = QStringLiteral("beat");
        beat.label = tr("Beat");
        beat.type = sdk::Field::Type::Choice;
        beat.choices = beatNames();
        beat.value = beatNames().value(event.beat);
        inspection.fields << beat << length;
    } else if (event.kind == SmbMusicEvent::Kind::Rest) {
        inspection.title = tr("Rest");
        inspection.fields << length;
    } else {
        const int pitch = music().pitchOf(event.periodIndex, channel.id);
        inspection.title = pitch >= 0 ? pitchName(pitch) : tr("Silent");
        sdk::Field period;
        period.key = QStringLiteral("period");
        period.label = tr("Period entry");
        period.type = sdk::Field::Type::Integer;
        period.value = event.periodIndex;
        period.minimum = channel.id == QLatin1String("square1") ? 0 : 1;
        period.maximum = channel.id == QLatin1String("square1") ? 31 : std::min(63, int(music().periods.size()) - 1);
        period.hint = tr("timer %1").arg(music().periods.value(event.periodIndex));
        period.monospace = true;
        inspection.fields << period << length;
        if (channel.id != QLatin1String("square1")) {
            sdk::Field run;
            run.key = QStringLiteral("run");
            run.label = tr("Run length");
            run.type = sdk::Field::Type::Choice;
            run.choices = lengthChoices;
            run.value = lengthChoices.value(lengthIndexFor(event.duration));
            inspection.fields << run;
        }
    }
    sdk::Field byte;
    byte.key = QStringLiteral("byte");
    byte.label = tr("Byte");
    byte.value = tr("%1 at %2").arg(hex(raw)).arg(hex(event.offset, 4));
    byte.monospace = true;
    inspection.fields << byte;
    const QStringList readers = music().readersOf(event.offset);
    sdk::Field shared;
    shared.key = QStringLiteral("shared");
    shared.label = tr("Heard in");
    shared.value = readers.join(QStringLiteral(", "));
    inspection.fields << shared;
    QString note = channel.id == QLatin1String("square1") || channel.id == QLatin1String("noise")
                       ? tr("This byte holds both the note and its length.")
                       : tr("On this channel a length byte sets the length of every note after it; Run "
                            "length changes that byte.");
    if (readers.size() > 1) {
        note += tr(" Other songs read this byte too and change with it.");
    }
    inspection.note = note;
    return inspection;
}

sdk::Budget SmbMusicModel::budget() const {
    sdk::Budget budget;
    budget.used = music().arena.size();
    budget.capacity = music().arena.size();
    budget.text = tr("stream arena %1 bytes · fixed").arg(music().arena.size());
    return budget;
}

QString SmbMusicModel::statusText() const {
    const SmbMusicTimeline& t = timeline();
    return tr("%1 · %2 ticks · %3 channels").arg(header_).arg(t.duration).arg(t.channels.size());
}

// ---------------------------------------------------------------- listening

void SmbMusicModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) {
        return;
    }
    // The kit renders from a bundle file: stage the current document.
    const QString directory = QDir(session_->projectDirectory()).filePath(QStringLiteral("build/sound-preview"));
    QDir().mkpath(directory);
    const QString document = QDir(directory).filePath(QStringLiteral("smb-resources.json"));
    const QString wav = QDir(directory).filePath(QStringLiteral("%1.wav").arg(header_));
    try {
        session_->document().save(document);
    } catch (const SmbError& e) {
        sdk::Diagnostic d;
        d.severity = sdk::Severity::Error;
        d.source = QStringLiteral("sound");
        d.message = QString::fromUtf8(e.what());
        host->report(d);
        return;
    }
    QStringList arguments{QStringLiteral("audio"), QStringLiteral("render-smb-wav"), document,
                          QStringLiteral("--output"), wav, QStringLiteral("--resource"), session_->music(),
                          QStringLiteral("--header"), header_};
    const QString selector = selectorOf();
    if (!selector.isEmpty()) {
        arguments << QStringLiteral("--selector") << selector;
    }
    for (const QString& channel : muted_) {
        arguments << QStringLiteral("--mute") << channel;
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
    spec.label = tr("Render %1").arg(header_);
    const QString header = header_;
    host->runProcess(spec, [host, wav, header](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                sdk::Diagnostic d;
                d.severity = sdk::Severity::Error;
                d.source = QStringLiteral("sound");
                d.message = tr("Rendering %1 failed: %2")
                                .arg(header, QString::fromUtf8(result.standardError).trimmed().section(QLatin1Char('\n'), -1));
                host->report(d);
            }
            return;
        }
        if (!QFileInfo(wav).isFile()) {
            sdk::Diagnostic d;
            d.severity = sdk::Severity::Error;
            d.source = QStringLiteral("sound");
            d.message = tr("The kit wrote no WAV for %1.").arg(header);
            host->report(d);
            return;
        }
        host->log(tr("  Playing %1").arg(QDir::toNativeSeparators(wav)));
        host->playAudio(wav);
    });
}

void SmbMusicModel::stopPreview() {
    if (sdk::Host* host = session_->host()) {
        host->stopAudio();
    }
}

bool SmbMusicModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void SmbMusicModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) {
        host->setAudioLoop(loop);
    }
}

}  // namespace rl::smb
