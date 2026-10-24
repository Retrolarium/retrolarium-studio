#include "alien_soldier/plugin/animations_model.hpp"

#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

// A duration whose low byte is negative holds the frame for good.
bool holds(int duration) { return duration & 0x80; }

}  // namespace

AnimationsModel::AnimationsModel(AlienSoldierSession* session, const QString& script)
    : MappingCanvas(session), script_(script) {
    // The first entry that is a frame.
    for (int index = 0; index < frames().size(); ++index) {
        if (entry(index).value(QStringLiteral("duration")).toInt() != 0) {
            frame_ = index;
            break;
        }
    }
}

QJsonObject AnimationsModel::entry(int index) const {
    const QJsonArray scripts = session_->doc(Doc::Animations).value(QStringLiteral("scripts")).toArray();
    return scripts.at(indexOfLabel(scripts, script_))
        .toObject()
        .value(QStringLiteral("entries"))
        .toArray()
        .at(index)
        .toObject();
}

QPair<int, int> AnimationsModel::mapping() const {
    const QJsonObject item = entry(frame_);
    if (item.value(QStringLiteral("duration")).toInt() == 0) return {-1, -1};
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    return {indexOfLabel(blocks, item.value(QStringLiteral("target")).toString()), 0};
}

QVector<sdk::CompositionFrame> AnimationsModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QJsonArray scripts = session_->doc(Doc::Animations).value(QStringLiteral("scripts")).toArray();
    const QJsonArray entries =
        scripts.at(indexOfLabel(scripts, script_)).toObject().value(QStringLiteral("entries")).toArray();
    for (int index = 0; index < entries.size(); ++index) {
        const QJsonObject item = entries.at(index).toObject();
        sdk::CompositionFrame frame;
        frame.id = QString::number(index);
        const int duration = item.value(QStringLiteral("duration")).toInt();
        frame.label = duration ? item.value(QStringLiteral("target")).toString()
                               : tr("→ %1").arg(item.value(QStringLiteral("target")).toString());
        frame.detail = !duration ? tr("jump") : holds(duration) ? tr("hold") : tr("%n calls", nullptr, duration & 0x7F);
        result.append(frame);
    }
    return result;
}

void AnimationsModel::setCurrentFrame(int frame) {
    frame_ = std::clamp(frame, 0, std::max(0, frames().size() - 1));
    clearPiece();
    emit selectionChanged();
    emit contentChanged();
}

int AnimationsModel::frameDurationMs(int frame) const {
    const int duration = entry(frame).value(QStringLiteral("duration")).toInt();
    // A held frame stays a second in the preview before it starts again.
    if (holds(duration)) return 1000;
    return (duration & 0x7F) * 1000 / 60;
}

QString AnimationsModel::statusText() const {
    return tr("%1 · entry %2 of %3").arg(script_).arg(frame_ + 1).arg(frames().size());
}

sdk::Inspection AnimationsModel::inspect() const {
    sdk::Inspection inspection;
    if (!selectedPiece().isEmpty()) {
        inspectPiece(inspection);
        return inspection;
    }
    const QJsonObject item = entry(frame_);
    inspection.kicker = tr("Animation");
    inspection.title = script_;
    sdk::Field target;
    target.key = QStringLiteral("target");
    target.label = item.value(QStringLiteral("duration")).toInt() ? tr("Mapping") : tr("Jumps to");
    target.value = item.value(QStringLiteral("target")).toString();
    target.type = sdk::Field::Type::Choice;
    const QJsonObject animations = session_->doc(Doc::Animations);
    if (item.value(QStringLiteral("duration")).toInt()) {
        for (const QJsonValue& value : animations.value(QStringLiteral("frames")).toArray())
            target.choices << value.toString();
    } else {
        for (const QJsonValue& value : animations.value(QStringLiteral("scripts")).toArray()) {
            target.choices << value.toObject().value(QStringLiteral("label")).toString();
        }
    }
    sdk::Field duration;
    duration.key = QStringLiteral("duration");
    duration.label = tr("Duration");
    duration.type =
        item.value(QStringLiteral("duration")).toInt() ? sdk::Field::Type::Integer : sdk::Field::Type::ReadOnly;
    duration.value = item.value(QStringLiteral("duration")).toInt();
    duration.minimum = 1;
    duration.maximum = 0xFFFF;
    if (holds(item.value(QStringLiteral("duration")).toInt())) duration.hint = tr("holds the frame for good");
    inspection.fields = {target, duration};
    inspection.note =
        tr("A frame lasts its duration in calls of the animation reader; a duration whose low byte is $80 or "
           "more holds the frame for good, which is how a script ends. An entry stays a frame or a jump.");
    return inspection;
}

bool AnimationsModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (!selectedPiece().isEmpty()) return setPieceField(key, value, error);
    if (key != QLatin1String("duration") && key != QLatin1String("target")) return false;
    const int frame = frame_;
    return session_->edit(
        Doc::Animations, tr("Change animation"),
        [&](QJsonObject& document) {
            QJsonArray scripts = document.value(QStringLiteral("scripts")).toArray();
            const int at = indexOfLabel(scripts, script_);
            QJsonObject script = scripts.at(at).toObject();
            QJsonArray entries = script.value(QStringLiteral("entries")).toArray();
            QJsonObject item = entries.at(frame).toObject();
            if (key == QLatin1String("duration")) {
                const int number = value.toInt();
                if (item.value(QStringLiteral("duration")).toInt() == 0 || number < 1 || number > 0xFFFF) {
                    throw std::runtime_error(tr("A frame lasts 1 to 65535 calls; a jump stays a jump.").toStdString());
                }
                item.insert(QStringLiteral("duration"), number);
            } else {
                item.insert(QStringLiteral("target"), value.toString());
            }
            entries.replace(frame, item);
            script.insert(QStringLiteral("entries"), entries);
            scripts.replace(at, script);
            document.insert(QStringLiteral("scripts"), scripts);
        },
        error);
}

}  // namespace rl::alien_soldier
