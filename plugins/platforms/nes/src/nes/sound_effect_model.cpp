#include "nes/sound_effect_model.hpp"

#include "nes/editors.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "sdk/session.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace rl::nes {

namespace {

const char* const kChannels[4] = {"Pulse 1", "Pulse 2", "Triangle", "Noise"};
const QColor kColours[4] = {QColor(0xe0, 0xb0, 0x40), QColor(0x60, 0xa8, 0xe0), QColor(0x70, 0xc8, 0x70),
                            QColor(0xb0, 0xb0, 0xb0)};
constexpr double kFrameRate = 60.0988;

// Bundle resources of a BundleOwner: the whole bundle is the document.
class BundleData final : public SoundEffectData {
public:
    BundleData(BundleOwner* owner, QStringList resources) : owner_(owner), resources_(std::move(resources)) {}

    // Called from signal handlers: a resource the bundle lacks (resource()
    // throws for it) counts as empty rather than escaping into Qt.
    QJsonObject state() const override {
        QJsonObject state;
        for (const QString& id : resources_) state.insert(id, resourceOf(owner_->bundle(), id));
        return state;
    }

    bool edited() const override {
        for (const QString& id : resources_) {
            if (resourceOf(owner_->bundle(), id) != resourceOf(owner_->savedBundle(), id)) return true;
        }
        return false;
    }

    bool writeDocument(const QString& path, QString* error) const override {
        try {
            owner_->bundle().save(path);
            return true;
        } catch (const std::exception& e) {
            if (error) *error = QString::fromUtf8(e.what());
            return false;
        }
    }

private:
    static QJsonObject resourceOf(const Bundle& bundle, const QString& id) {
        return bundle.contains(id) ? bundle.resource(id) : QJsonObject();
    }

    BundleOwner* owner_;
    QStringList resources_;
};

}  // namespace

bool SoundEffectData::setTableValue(const QString&, int, int, const QString&, QString* error) {
    if (error) *error = rlTr("nes", "This effect's data is not editable here.");
    return false;
}

std::shared_ptr<SoundEffectData> bundleSoundEffectData(BundleOwner* owner, QStringList resources) {
    return std::make_shared<BundleData>(owner, std::move(resources));
}

SoundEffectModel::SoundEffectModel(sdk::Session* session, SoundEffect effect, QString projectPath,
                                   QString projectDirectory, std::shared_ptr<SoundEffectData> data)
    : SequencerModel(session),
      session_(session),
      effect_(std::move(effect)),
      projectPath_(std::move(projectPath)),
      projectDirectory_(std::move(projectDirectory)),
      data_(std::move(data)) {
    // An edit to the effect's data traces it again.
    connect(session_, &sdk::Session::contentChanged, this, [this] {
        if (data_ && data_->state() != tracedState_) requestTrace();
    });
    requestTrace();
}

QString SoundEffectModel::outputPath(const QString& suffix) const {
    QString name = effect_.key;
    name.replace(QLatin1Char(':'), QLatin1Char('-'));
    return QDir(projectDirectory_).filePath(QStringLiteral("build/sound-preview/sfx-%1.%2").arg(name, suffix));
}

QStringList SoundEffectModel::arguments(QString* error) const {
    QStringList result{QStringLiteral("audio"), QStringLiteral("render-sfx"), projectPath_, QStringLiteral("--effect"),
                       effect_.key};
    if (data_ && data_->edited()) {
        const QString document = outputPath(QStringLiteral("bundle.json"));
        QDir().mkpath(QFileInfo(document).absolutePath());
        if (!data_->writeDocument(document, error)) return {};
        result << QStringLiteral("--document") << document;
    }
    return result;
}

void SoundEffectModel::report(const QString& message) const {
    sdk::Host* host = session_->host();
    if (!host) return;
    sdk::Diagnostic diagnostic;
    diagnostic.severity = sdk::Severity::Error;
    diagnostic.source = QStringLiteral("sound");
    diagnostic.message = message;
    diagnostic.location = effect_.title;
    host->report(diagnostic);
}

void SoundEffectModel::requestTrace() {
    sdk::Host* host = session_->host();
    if (!host || tracing_) return;
    if (host->isBusy()) {
        // Another process runs (a build, a render): try again shortly.
        QTimer::singleShot(400, this, &SoundEffectModel::requestTrace);
        return;
    }
    QString error;
    QStringList args = arguments(&error);
    if (args.isEmpty()) {
        report(error);
        return;
    }
    const QString output = outputPath(QStringLiteral("json"));
    args << QStringLiteral("--trace") << output;
    sdk::ProcessSpec spec = host->kitCommand(args, projectDirectory_, &error);
    if (spec.program.isEmpty()) {
        report(error);
        return;
    }
    spec.label = tr("Trace %1").arg(effect_.title);
    tracing_ = true;
    const QJsonObject state = data_ ? data_->state() : QJsonObject();
    QPointer<SoundEffectModel> self(this);
    host->runProcess(spec, [self, output, state](const sdk::ProcessResult& result) {
        if (!self) return;
        self->tracing_ = false;
        if (!result.succeeded()) {
            if (!result.cancelled) self->report(tr("Tracing %1 failed.").arg(self->effect_.title));
            return;
        }
        QFile file(output);
        if (!file.open(QIODevice::ReadOnly)) {
            self->report(tr("Cannot read %1.").arg(output));
            return;
        }
        const bool first = self->trace_.isEmpty();
        self->trace_ = QJsonDocument::fromJson(file.readAll()).object();
        self->tracedState_ = state;
        // Show the first channel the effect plays on.
        const QJsonArray lanes = self->trace_.value(QStringLiteral("channels")).toArray();
        if (first && lanes.at(self->channel_).toObject().value(QStringLiteral("notes")).toArray().isEmpty()) {
            for (int c = 0; c < lanes.size(); ++c) {
                if (!lanes.at(c).toObject().value(QStringLiteral("notes")).toArray().isEmpty()) {
                    self->channel_ = c;
                    break;
                }
            }
        }
        emit self->toolsChanged();
        emit self->contentChanged();
        emit self->selectionChanged();
        // An edit made while tracing is traced next.
        if (self->data_ && self->data_->state() != state) self->requestTrace();
    });
}

bool SoundEffectModel::hasTables() const {
    return data_ && !trace_.value(QStringLiteral("tables")).toArray().isEmpty();
}

QJsonObject SoundEffectModel::noteAt(const QString& id) const {
    const int channel = id.section(QLatin1Char(':'), 0, 0).toInt();
    const int index = id.section(QLatin1Char(':'), 1, 1).toInt();
    return trace_.value(QStringLiteral("channels"))
        .toArray()
        .at(channel)
        .toObject()
        .value(QStringLiteral("notes"))
        .toArray()
        .at(index)
        .toObject();
}

QJsonObject SoundEffectModel::tableOf(const QString& table) const {
    for (const QJsonValue& v : trace_.value(QStringLiteral("tables")).toArray()) {
        if (v.toObject().value(QStringLiteral("id")).toString() == table) return v.toObject();
    }
    return {};
}

int SoundEffectModel::valueFor(const QJsonObject& source, int pitch) const {
    const QJsonObject table = tableOf(source.value(QStringLiteral("table")).toString());
    const int current = source.value(QStringLiteral("value")).toInt();
    const bool noise = table.value(QStringLiteral("kind")).toString() == QLatin1String("noise");
    int best = -1;
    for (const QJsonValue& v : table.value(QStringLiteral("choices")).toArray()) {
        const QJsonObject choice = v.toObject();
        if (choice.value(QStringLiteral("pitch")).toInt() != pitch) continue;
        const int value = choice.value(QStringLiteral("value")).toInt();
        // Of equal pitches, the one nearest the current byte.
        if (best < 0 || std::abs(value - current) < std::abs(best - current)) best = value;
    }
    if (best < 0) return -1;
    return noise ? (current & 0xF0) | best : best;
}

int SoundEffectModel::nearestPitch(const QJsonObject& source, int pitch) const {
    const QJsonObject table = tableOf(source.value(QStringLiteral("table")).toString());
    int best = -1;
    for (const QJsonValue& v : table.value(QStringLiteral("choices")).toArray()) {
        const int p = v.toObject().value(QStringLiteral("pitch")).toInt();
        if (best < 0 || std::abs(p - pitch) < std::abs(best - pitch)) best = p;
    }
    return best;
}

bool SoundEffectModel::setSourceValue(const QJsonObject& source, int value, QString* error) {
    if (!data_) return false;
    const QString table = source.value(QStringLiteral("table")).toString();
    const int index = source.value(QStringLiteral("index")).toInt();
    if (value == source.value(QStringLiteral("value")).toInt()) return false;
    return data_->setTableValue(table, index, value, tr("Edit %1 (%2[%3])").arg(effect_.title, table).arg(index),
                                error);
}

QVector<sdk::SequencerChannel> SoundEffectModel::channels() const {
    QVector<sdk::SequencerChannel> result;
    const QJsonArray lanes = trace_.value(QStringLiteral("channels")).toArray();
    for (int c = 0; c < 4; ++c) {
        sdk::SequencerChannel channel;
        channel.id = QString::number(c);
        channel.label = QString::fromLatin1(kChannels[c]);
        const int count = lanes.at(c).toObject().value(QStringLiteral("notes")).toArray().size();
        channel.detail = count ? tr("%n notes", nullptr, count) : tr("unused");
        channel.color = kColours[c];
        channel.muted = muted_.contains(c);
        result << channel;
    }
    return result;
}

void SoundEffectModel::setCurrentChannel(int channel) {
    if (channel != channel_ && channel >= 0 && channel < 4) {
        channel_ = channel;
        emit selectionChanged();
    }
}

void SoundEffectModel::setChannelMuted(int channel, bool muted) {
    if (muted) muted_.insert(channel);
    else muted_.remove(channel);
    emit toolsChanged();
}

QVector<sdk::SequencerNote> SoundEffectModel::notes(int channel) const {
    QVector<sdk::SequencerNote> result;
    const QJsonArray notes = trace_.value(QStringLiteral("channels"))
                                 .toArray()
                                 .at(channel)
                                 .toObject()
                                 .value(QStringLiteral("notes"))
                                 .toArray();
    for (int i = 0; i < notes.size(); ++i) {
        const QJsonObject n = notes.at(i).toObject();
        sdk::SequencerNote note;
        note.id = QStringLiteral("%1:%2").arg(channel).arg(i);
        note.start = n.value(QStringLiteral("start")).toInt();
        note.length = std::max(1, n.value(QStringLiteral("length")).toInt());
        const int pitch =
            note.id == dragging_ && dragPitch_ >= 0 ? dragPitch_ : n.value(QStringLiteral("pitch")).toInt();
        note.pitch = std::clamp(pitch, pitchMinimum(), pitchMaximum());
        note.pitchEditable =
            data_ && n.contains(QStringLiteral("source")) &&
            !tableOf(n.value(QStringLiteral("source")).toObject().value(QStringLiteral("table")).toString()).isEmpty();
        result << note;
    }
    return result;
}

void SoundEffectModel::selectNote(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        if (!id.isEmpty()) channel_ = id.section(QLatin1Char(':'), 0, 0).toInt();
        emit selectionChanged();
    }
}

void SoundEffectModel::beginEdit(const QString& id) {
    selectNote(id);
    if (!noteAt(id).contains(QStringLiteral("source"))) return;
    dragging_ = id;
    dragPitch_ = noteAt(id).value(QStringLiteral("pitch")).toInt();
}

bool SoundEffectModel::editNote(const QString& id, int pitch, int) {
    if (id != dragging_) return false;
    // Only the pitches the table's values play.
    const int snapped = nearestPitch(noteAt(id).value(QStringLiteral("source")).toObject(), pitch);
    if (snapped < 0 || snapped == dragPitch_) return false;
    dragPitch_ = snapped;
    emit contentChanged();
    return true;
}

void SoundEffectModel::endEdit() {
    const QString id = dragging_;
    const int pitch = dragPitch_;
    dragging_.clear();
    dragPitch_ = -1;
    if (id.isEmpty()) return;
    const QJsonObject note = noteAt(id);
    if (pitch != note.value(QStringLiteral("pitch")).toInt()) {
        const QJsonObject source = note.value(QStringLiteral("source")).toObject();
        QString error;
        const int value = valueFor(source, pitch);
        if (value >= 0 && !setSourceValue(source, value, &error) && !error.isEmpty()) report(error);
    }
    emit contentChanged();
}

QString SoundEffectModel::readOnlyReason() const {
    return tr("Sound effects are code in this game: the notes show what its driver plays.");
}

sdk::Inspection SoundEffectModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Sound effect");
    inspection.title = effect_.title;
    inspection.engineTag = effect_.key;
    sdk::Field trigger;
    trigger.key = QStringLiteral("trigger");
    trigger.label = tr("Started by");
    trigger.value = effect_.trigger;
    sdk::Field length;
    length.key = QStringLiteral("length");
    length.label = tr("Length");
    const int frames = trace_.value(QStringLiteral("frames")).toInt();
    length.value =
        trace_.isEmpty() ? tr("tracing…") : tr("%1 frames · %2 s").arg(frames).arg(frames / kFrameRate, 0, 'f', 2);
    inspection.fields = {trigger, length};
    if (!selected_.isEmpty()) {
        const int channel = std::clamp(selected_.section(QLatin1Char(':'), 0, 0).toInt(), 0, 3);
        const QJsonObject n = noteAt(selected_);
        sdk::Field note;
        note.key = QStringLiteral("note");
        note.label = QString::fromLatin1(kChannels[channel]);
        note.value = tr("%1 · frame %2 · %3 frames")
                         .arg(pitchName(n.value(QStringLiteral("pitch")).toInt()))
                         .arg(n.value(QStringLiteral("start")).toInt())
                         .arg(n.value(QStringLiteral("length")).toInt());
        inspection.fields << note;
        const QJsonObject source = n.value(QStringLiteral("source")).toObject();
        const QJsonObject table = tableOf(source.value(QStringLiteral("table")).toString());
        if (data_ && !table.isEmpty()) {
            // The table byte behind the note, as one of the pitches it can play.
            sdk::Field value;
            value.key = QStringLiteral("value");
            value.label = tr("Table byte");
            value.type = sdk::Field::Type::Choice;
            value.hint = QStringLiteral("%1[%2]")
                             .arg(source.value(QStringLiteral("table")).toString())
                             .arg(source.value(QStringLiteral("index")).toInt());
            const int current = source.value(QStringLiteral("value")).toInt();
            const bool noise = table.value(QStringLiteral("kind")).toString() == QLatin1String("noise");
            for (const QJsonValue& v : table.value(QStringLiteral("choices")).toArray()) {
                const QJsonObject choice = v.toObject();
                const int byte = noise ? (current & 0xF0) | choice.value(QStringLiteral("value")).toInt()
                                       : choice.value(QStringLiteral("value")).toInt();
                const QString label = noise ? tr("%1 · period %2").arg(hex(byte)).arg(byte & 0x0F)
                                            : QStringLiteral("%1 · %2").arg(
                                                  hex(byte), pitchName(choice.value(QStringLiteral("pitch")).toInt()));
                value.choices << label;
                if (byte == current) value.value = label;
            }
            if (value.value.isNull()) value.value = hex(current);
            inspection.fields << value;
        }
    }
    inspection.note =
        tr("Played through the game's own sound code: the kit runs the effect's routines "
           "frame by frame and records the APU registers. One tick is one frame.");
    if (hasTables()) {
        inspection.note +=
            tr(" Notes set from a table byte move up or down to the pitches the table can "
               "hold; that byte changes for every place the effect reads it.");
    }
    if (!effect_.note.isEmpty()) inspection.note += QLatin1Char(' ') + effect_.note;
    return inspection;
}

bool SoundEffectModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key != QLatin1String("value") || selected_.isEmpty()) return SequencerModel::setField(key, value, error);
    const QJsonObject source = noteAt(selected_).value(QStringLiteral("source")).toObject();
    const int byte = value.toString().section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('$')).toInt(nullptr, 16);
    if (source.isEmpty() || byte < 0 || byte > 0xFF) return false;
    return setSourceValue(source, byte, error);
}

QString SoundEffectModel::statusText() const { return tr("Sound effect · %1").arg(effect_.title); }

void SoundEffectModel::preview() {
    sdk::Host* host = session_->host();
    if (!host || host->isBusy()) return;
    QString error;
    QStringList args = arguments(&error);
    if (args.isEmpty()) {
        report(error);
        return;
    }
    const QString wav = outputPath(QStringLiteral("wav"));
    args << QStringLiteral("--wav") << wav;
    if (!muted_.isEmpty()) {
        for (int c = 0; c < 4; ++c) {
            if (!muted_.contains(c)) args << QStringLiteral("--channel") << QString::fromLatin1(kChannels[c]);
        }
    }
    sdk::ProcessSpec spec = host->kitCommand(args, projectDirectory_, &error);
    if (spec.program.isEmpty()) {
        report(error);
        return;
    }
    spec.label = tr("Render %1").arg(effect_.title);
    QPointer<SoundEffectModel> self(this);
    host->runProcess(spec, [self, host, wav](const sdk::ProcessResult& result) {
        if (!result.succeeded() || !QFileInfo(wav).isFile()) {
            if (self && !result.cancelled) self->report(tr("Rendering %1 failed.").arg(self->effect_.title));
            return;
        }
        host->playAudio(wav);
    });
}

void SoundEffectModel::stopPreview() {
    if (sdk::Host* host = session_->host()) host->stopAudio();
}

bool SoundEffectModel::previewLoops() const {
    sdk::Host* host = session_->host();
    return host && host->audioLoops();
}

void SoundEffectModel::setPreviewLoops(bool loop) {
    if (sdk::Host* host = session_->host()) host->setAudioLoop(loop);
}

}  // namespace rl::nes
