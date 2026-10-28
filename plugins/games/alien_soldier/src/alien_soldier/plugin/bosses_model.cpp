#include "alien_soldier/plugin/bosses_model.hpp"

#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace rl::alien_soldier {

namespace {

constexpr int kMaxFrames = 900;
constexpr int kFrameMs = 17;  // one 60 Hz frame
constexpr int kSoundCommand = 0x80;
constexpr double kPi = 3.14159265358979323846;
// How far a press may wander before it is a drag, in pixels across plus
// down.
constexpr int kClickSlop = 3;

QString hex(int value) { return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(); }

sdk::Field integerField(const QString& key, const QString& label, int value, int minimum, int maximum) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Integer;
    field.value = value;
    field.minimum = minimum;
    field.maximum = maximum;
    field.monospace = true;
    return field;
}

sdk::Field readOnlyField(const QString& key, const QString& label, const QString& value) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.value = value;
    field.monospace = true;
    return field;
}

}  // namespace

BossesModel::BossesModel(AlienSoldierSession* session, const QString& document)
    : CompositionModel(session), session_(session) {
    const QStringList parts = document.split(QLatin1Char(':'));
    if (parts.value(0) == QLatin1String("anim") && parts.size() == 3) {
        mode_ = Mode::Animation;
        bossId_ = parts[1];
        label_ = parts[2];
    } else if (parts.value(0) == QLatin1String("poses") && parts.size() == 2) {
        mode_ = Mode::Poses;
        bossId_ = parts[1];
    } else {
        stage_ = parts.value(1).toInt();
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Bosses || doc == Doc::Mappings || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes) {
            if (doc != Doc::Bosses && doc != Doc::Mappings) contextReady_ = false;
            emit contentChanged();
        }
    });
}

QJsonObject BossesModel::boss() const {
    return mode_ == Mode::Code ? QJsonObject() : bossById(session_->docs(), bossId_);
}

QJsonArray BossesModel::poseRows() const {
    return session_->doc(Doc::Bosses)
        .value(QStringLiteral("poses"))
        .toObject()
        .value(boss().value(QStringLiteral("poses")).toString())
        .toArray();
}

const PoseRun& BossesModel::run() const {
    stepped();
    return run_;
}

const PoseFrames& BossesModel::stepped() const {
    const quint64 revision = session_->revision(Doc::Bosses);
    if (revision != cachedRevision_) {
        cachedRevision_ = revision;
        focus_ = QRect();
        run_ = mode_ == Mode::Animation ? poseRun(session_->doc(Doc::Bosses), label_) : PoseRun();
        // The game enters an animation from wherever the last one left the
        // channels; it starts here from its first pose.
        QVector<int> start;
        const auto first = std::find_if(run_.commands.begin(), run_.commands.end(),
                                        [](const PoseCommand& command) { return !command.sound; });
        if (first != run_.commands.end()) start = poseValues(poseRows(), first->value);
        stepped_ = stepPoses(boss(), poseRows(), run_, start, kMaxFrames);
    }
    return stepped_;
}

QVector<int> BossesModel::keyframes() const {
    QVector<int> found;
    for (int index = 0; index < run().commands.size(); ++index)
        if (!run().commands[index].sound) found.append(index);
    return found;
}

QVector<int> BossesModel::channelsAt(int frame) const {
    QVector<int> channels;
    if (mode_ == Mode::Poses) {
        for (int value : poseValues(poseRows(), frame)) channels.append(value << 8);
        return channels;
    }
    const PoseFrames& frames = stepped();
    if (frames.channels.isEmpty()) return channels;
    if (everyFrame_) return frames.channels.value(std::clamp(frame, 0, int(frames.channels.size()) - 1));
    const QVector<int> keys = keyframes();
    if (frame < 0 || frame >= keys.size()) return frames.channels.last();
    const int end = frames.ends.value(keys[frame], -1);
    return end < 0 ? frames.channels.last() : frames.channels[end];
}

int BossesModel::editedPose() const {
    if (mode_ == Mode::Poses) return frame_;
    if (mode_ != Mode::Animation || everyFrame_) return -1;
    const QVector<int> keys = keyframes();
    return frame_ >= 0 && frame_ < keys.size() ? run().commands[keys[frame_]].value : -1;
}

QVector<PlacedPart> BossesModel::placed(int frame) const {
    const QJsonArray cosine = session_->doc(Doc::Bosses).value(QStringLiteral("cosine")).toArray();
    return placeBoss(boss(), cosine, channelsAt(frame), stance());
}

BossStance BossesModel::stance() const {
    // A boss whose code never turns it (Wolf Garopa, its body built facing
    // one way) stands as it is built.
    BossStance kept = stance_;
    const QJsonValue faces = boss().value(QStringLiteral("faces"));
    if (faces.isDouble()) kept.facing = faces.toInt();
    return kept;
}

void BossesModel::refresh() {
    focus_ = QRect();
    emit toolsChanged();
    emit contentChanged();
}

QString BossesModel::title() const {
    if (mode_ == Mode::Code) return session_->stageLabel(stage_);
    const QString name = boss().value(QStringLiteral("title")).toString();
    return mode_ == Mode::Poses ? tr("%1 · poses").arg(name) : tr("%1 · %2").arg(name, animationTitle(label_, name));
}

QString BossesModel::readOnlyReason() const {
    return tr("The game draws this boss with its own code: the kit does not read it as a skeleton yet.");
}

QString BossesModel::statusText() const {
    if (mode_ == Mode::Code) return title();
    const QJsonObject record = boss();
    return tr("%1 · %2 parts · %3 channels")
        .arg(title())
        .arg(record.value(QStringLiteral("parts")).toArray().size())
        .arg(record.value(QStringLiteral("channels")).toInt());
}

QVector<sdk::ViewOption> BossesModel::viewOptions() const {
    if (mode_ == Mode::Code) return {};
    QVector<sdk::ViewOption> options;
    const QJsonObject record = boss();
    if (!record.value(QStringLiteral("faces")).isDouble()) {
        sdk::ViewOption facing;
        facing.key = QStringLiteral("facing");
        facing.label = tr("Faces");
        facing.choices = QStringList{tr("Left"), tr("Right")};
        facing.value = stance_.facing >= 0x100 ? facing.choices[0] : facing.choices[1];
        options.append(facing);
    }
    if (!record.value(QStringLiteral("side_field")).isNull()) {
        sdk::ViewOption side;
        side.key = QStringLiteral("side");
        side.label = tr("Sides");
        side.choices = QStringList{tr("As built"), tr("Swapped")};
        side.value = side.choices[stance_.side ? 1 : 0];
        options.append(side);
    }
    if (mode_ == Mode::Animation) {
        sdk::ViewOption steps;
        steps.key = QStringLiteral("steps");
        steps.label = tr("Shows");
        steps.choices = QStringList{tr("Keyframes"), tr("Every frame")};
        steps.value = steps.choices[everyFrame_ ? 1 : 0];
        options.append(steps);
    }
    return options;
}

void BossesModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("facing")) stance_.facing = value == tr("Left") ? 0x100 : 0;
    if (key == QLatin1String("side")) stance_.side = value == tr("Swapped") ? 2 : 0;
    if (key == QLatin1String("steps")) {
        everyFrame_ = value == tr("Every frame");
        frame_ = 0;
    }
    piece_.clear();
    refresh();
}

QSize BossesModel::canvasSize() const {
    const QRect body = bodyRect(boss());
    const int across = std::max(std::abs(body.left()), std::abs(body.right() + 1));
    const int down = std::max(std::abs(body.top()), std::abs(body.bottom() + 1)) + 32;
    return {std::max(512, 2 * across + 64), std::max(512, 2 * down + 64)};
}

QPoint BossesModel::origin() const {
    const QSize size = canvasSize();
    return {size.width() / 2, size.height() / 2 + 32};
}

QImage BossesModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QJsonObject record = boss();
    if (record.isEmpty()) return image;
    if (!contextReady_) {
        context_ = bossContext(session_->docs(), record);
        contextReady_ = true;
    }
    drawBoss(image, origin(), record, placed(frame_), session_->docs(), context_);
    return image;
}

QRect BossesModel::focusRect() const {
    stepped();
    if (focus_.isEmpty()) {
        const QJsonObject record = boss();
        focus_ = bodyRect(record).translated(origin());
        const int count = int(frames().size());
        for (int frame = 0; frame < std::max(1, count); ++frame) {
            for (const PlacedPart& part : placed(frame))
                focus_ |= partRect(record, part, session_->docs()).translated(origin());
        }
        focus_ =
            focus_.isEmpty() ? QRect(origin() - QPoint(64, 64), QSize(128, 128)) : focus_.adjusted(-16, -16, 16, 16);
    }
    return focus_;
}

QVector<sdk::CompositionPiece> BossesModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const QJsonObject record = boss();
    if (record.isEmpty()) return result;
    const QJsonArray joints = record.value(QStringLiteral("joints")).toArray();
    const bool editable = editedPose() >= 0;
    for (const PlacedPart& part : placed(frame_)) {
        const QRect rect = partRect(record, part, session_->docs());
        if (rect.isEmpty()) continue;
        sdk::CompositionPiece piece;
        piece.id = QString::number(part.index);
        piece.label = tr("Part %1").arg(part.index);
        piece.pixels = rect.translated(origin());
        piece.movable = editable && !joints.at(part.index).isNull();
        result.append(piece);
    }
    return result;
}

void BossesModel::selectPiece(const QString& id) {
    piece_ = id;
    emit selectionChanged();
}

void BossesModel::beginDrag(const QString& id) {
    selectPiece(id);
    dragBefore_ = session_->doc(Doc::Bosses);
    dragRevision_ = session_->revision(Doc::Bosses);
    const QVector<PlacedPart> parts = placed(frame_);
    const int index = id.toInt();
    dragPart_ = parts.value(index);
    const QJsonValue parent =
        boss().value(QStringLiteral("parts")).toArray().at(index).toObject().value(QStringLiteral("parent"));
    dragParent_ = parent.isNull() ? QPoint() : parts.value(parent.toInt()).at;
}

bool BossesModel::dragPiece(const QString& id, QPoint offset) {
    const int pose = editedPose();
    const QJsonObject record = boss();
    const QJsonValue joint = record.value(QStringLiteral("joints")).toArray().at(id.toInt());
    if (pose < 0 || joint.isNull()) return false;
    // A click is a drag that goes nowhere, give or take a hand's shake: it
    // changes nothing.
    if (std::abs(offset.x()) + std::abs(offset.y()) < kClickSlop) return true;
    // The joint turns by as much as the bone from the parent towards the
    // part turns as it is dragged (the facing turns X round), from where
    // the pose had it when the drag began.
    const auto angleOf = [this](QPoint bone) {
        const int dx = stance().facing >= 0x100 ? -bone.x() : bone.x();
        return int(std::lround(std::atan2(double(bone.y()), double(dx)) * 128.0 / kPi));
    };
    const QPoint from = dragPart_.at - dragParent_;
    const QPoint to = from + offset;
    if (from.isNull() || to.isNull()) return true;
    const QString table = record.value(QStringLiteral("poses")).toString();
    const QJsonArray before = dragBefore_.value(QStringLiteral("poses")).toObject().value(table).toArray();
    const int channel = joint.toInt();
    const int value = (poseValues(before, pose).value(channel) + qint8(quint8(angleOf(to) - angleOf(from)))) & 0xFF;
    if (poseValues(poseRows(), pose).value(channel) == value) return true;
    QJsonObject& document = session_->live(Doc::Bosses);
    QJsonObject poses = document.value(QStringLiteral("poses")).toObject();
    QJsonArray rows = poses.value(table).toArray();
    QJsonObject row = rows.at(pose).toObject();
    QJsonArray changed = row.value(QStringLiteral("values")).toArray();
    changed.replace(channel, value);
    row.insert(QStringLiteral("values"), changed);
    rows.replace(pose, row);
    poses.insert(table, rows);
    document.insert(QStringLiteral("poses"), poses);
    session_->touch(Doc::Bosses);
    return true;
}

void BossesModel::endDrag() { session_->commit(Doc::Bosses, dragBefore_, dragRevision_, tr("Turn boss joint")); }

QVector<sdk::CompositionFrame> BossesModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    if (mode_ == Mode::Poses) {
        // A boss its code alone turns (Deep Strider) shows as its setup
        // leaves it.
        if (boss().value(QStringLiteral("poses")).isNull()) {
            sdk::CompositionFrame frame;
            frame.id = QStringLiteral("0");
            frame.label = tr("Set by its code");
            result.append(frame);
            return result;
        }
        for (int index = 0; index < poseRows().size(); ++index) {
            sdk::CompositionFrame frame;
            frame.id = QString::number(index);
            frame.label = tr("Pose %1").arg(index);
            result.append(frame);
        }
        return result;
    }
    if (mode_ != Mode::Animation) return result;
    const PoseRun& commands = run();
    if (everyFrame_) {
        const PoseFrames& frames = stepped();
        for (int index = 0; index < frames.channels.size(); ++index) {
            sdk::CompositionFrame frame;
            frame.id = QString::number(index);
            frame.label = QString::number(index + 1);
            const int command = frames.command.value(index, -1);
            frame.detail = command >= 0 ? tr("pose %1").arg(commands.commands[command].value) : QString();
            result.append(frame);
        }
        return result;
    }
    QStringList sounds;
    for (int index = 0; index < commands.commands.size(); ++index) {
        const PoseCommand& command = commands.commands[index];
        if (command.sound) {
            sounds << tr("sound %1").arg(hex(command.value));
            continue;
        }
        sdk::CompositionFrame frame;
        frame.id = QString::number(index);
        frame.label = tr("Pose %1").arg(command.value);
        frame.detail = (QStringList{tr("%n frames", nullptr, std::max(0, commandFrames(boss(), command)))} + sounds)
                           .join(QStringLiteral(" · "));
        sounds.clear();
        result.append(frame);
    }
    return result;
}

void BossesModel::setCurrentFrame(int frame) {
    frame_ = std::clamp(frame, 0, std::max(0, int(frames().size()) - 1));
    emit selectionChanged();
    emit contentChanged();
}

int BossesModel::frameDurationMs(int frame) const {
    if (mode_ == Mode::Poses) return 400;
    if (everyFrame_) return kFrameMs;
    const QVector<int> keys = keyframes();
    return frame >= 0 && frame < keys.size()
               ? std::max(1, commandFrames(boss(), run().commands[keys[frame]])) * kFrameMs
               : kFrameMs;
}

sdk::Inspection BossesModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject record = boss();
    if (record.isEmpty()) {
        inspection.kicker = tr("Boss");
        inspection.title = title();
        inspection.note = readOnlyReason();
        return inspection;
    }
    const int pose = editedPose();
    const QVector<int> row = poseValues(poseRows(), std::max(0, pose));
    const QVector<int> shown = channelsAt(frame_);
    if (!piece_.isEmpty()) {
        const int index = piece_.toInt();
        const QJsonObject part = record.value(QStringLiteral("parts")).toArray().at(index).toObject();
        const QJsonValue joint = record.value(QStringLiteral("joints")).toArray().at(index);
        inspection.kicker = tr("Boss part");
        inspection.title = tr("Part %1").arg(index);
        const QJsonValue parent = part.value(QStringLiteral("parent"));
        inspection.fields = {
            readOnlyField(QStringLiteral("draws"), tr("Draws"),
                          part.value(QStringLiteral("source")).toString(part.value(QStringLiteral("kind")).toString())),
            readOnlyField(QStringLiteral("parent"), tr("Hangs from"),
                          parent.isNull() ? tr("the boss itself") : tr("part %1").arg(parent.toInt())),
            readOnlyField(QStringLiteral("radius"), tr("Bone"),
                          tr("%n pixels", nullptr, part.value(QStringLiteral("radius")).toInt())),
            readOnlyField(QStringLiteral("depth"), tr("Depth"),
                          QString::number(part.value(QStringLiteral("depth")).toInt())),
        };
        if (!joint.isNull()) {
            const int channel = joint.toInt();
            if (pose >= 0) {
                inspection.fields.append(integerField(QStringLiteral("channel:%1").arg(channel),
                                                      tr("Joint (channel %1)").arg(channel), row.value(channel), 0,
                                                      255));
            } else {
                inspection.fields.append(readOnlyField(QStringLiteral("joint"), tr("Joint (channel %1)").arg(channel),
                                                       QString::number(shown.value(channel) >> 8)));
            }
        }
        inspection.note =
            tr("A part sits at the end of its bone from its parent, turned by the channels its joint "
               "adds up. Drag it to turn its own joint in the pose shown.");
        return inspection;
    }
    if (mode_ == Mode::Poses) {
        inspection.kicker = tr("Boss pose");
        inspection.title = tr("Pose %1").arg(pose);
    } else {
        inspection.kicker = tr("Boss animation");
        inspection.title = animationTitle(label_, record.value(QStringLiteral("title")).toString());
        const QVector<int> keys = keyframes();
        if (pose >= 0 && frame_ < keys.size()) {
            const PoseCommand& command = run().commands[keys[frame_]];
            const bool adding = record.value(QStringLiteral("timing")).toString() == QLatin1String("add");
            sdk::Field target;
            target.key = QStringLiteral("pose");
            target.label = tr("Pose");
            target.type = sdk::Field::Type::Choice;
            for (int index = 0; index < poseRows().size(); ++index) target.choices << QString::number(index);
            target.value = QString::number(command.value);
            inspection.fields = {
                target,
                adding ? integerField(QStringLiteral("frames"), tr("Frames over steps"),
                                      command.frames >= 0x80 ? command.frames - 0x100 : command.frames, -127, 127)
                       : integerField(QStringLiteral("frames"), tr("Frames"), command.frames, 0, 255),
                integerField(QStringLiteral("steps"), tr("Steps"), command.steps, 1, 255)};
        }
        const QJsonArray sequences = session_->doc(Doc::Bosses).value(QStringLiteral("sequences")).toArray();
        const QJsonObject sequence = sequences.at(indexOfLabel(sequences, label_)).toObject();
        const QString end = sequence.value(QStringLiteral("end")).toString();
        inspection.engineTag = QStringLiteral("alien-soldier.bosses.v1");
        inspection.engine = {
            {tr("Label"), label_},
            {tr("Ends"), end == QLatin1String("loop") ? tr("starts again")
                         : end == QLatin1String("hold")
                             ? tr("holds the last pose")
                             : tr("runs on into %1").arg(sequence.value(QStringLiteral("next")).toString())}};
    }
    for (int channel = 0; channel < record.value(QStringLiteral("channels")).toInt(); ++channel) {
        if (pose >= 0) {
            inspection.fields.append(integerField(QStringLiteral("channel:%1").arg(channel),
                                                  tr("Channel %1").arg(channel), row.value(channel), 0, 255));
        } else {
            inspection.fields.append(readOnlyField(QStringLiteral("channel:%1").arg(channel),
                                                   tr("Channel %1").arg(channel),
                                                   QString::number(shown.value(channel) >> 8)));
        }
    }
    if (!record.value(QStringLiteral("checked")).toBool()) {
        inspection.fields.prepend(
            readOnlyField(QStringLiteral("checked"), tr("Checked"),
                          tr("not yet against the game: its own code may place some parts elsewhere")));
    }
    inspection.note =
        mode_ == Mode::Poses
            ? tr("A pose is an angle per channel; the boss's pose code adds channels up into its parts' angles. "
                 "Every animation that goes to this pose changes with it.")
            : tr("A keyframe steps towards its pose by 1/steps of the way each frame, for frames + 1 frames, from "
                 "where the last one left off; the canvas shows where it lands. The channels are its pose's, shared "
                 "with every animation that goes to it. The game's code picks the animation and moves the boss.");
    return inspection;
}

bool BossesModel::setPoseChannel(int pose, int channel, int value, QString* error) {
    const QJsonObject record = boss();
    if (pose < 0 || channel < 0 || channel >= record.value(QStringLiteral("channels")).toInt() || value < 0 ||
        value > 0xFF) {
        if (error) *error = tr("A channel holds 0-255.");
        return false;
    }
    const QString table = record.value(QStringLiteral("poses")).toString();
    return session_->edit(
        Doc::Bosses, tr("Change boss pose"),
        [&](QJsonObject& document) {
            QJsonObject poses = document.value(QStringLiteral("poses")).toObject();
            QJsonArray rows = poses.value(table).toArray();
            QJsonObject row = rows.at(pose).toObject();
            QJsonArray changed = row.value(QStringLiteral("values")).toArray();
            changed.replace(channel, value);
            row.insert(QStringLiteral("values"), changed);
            rows.replace(pose, row);
            poses.insert(table, rows);
            document.insert(QStringLiteral("poses"), poses);
        },
        error);
}

bool BossesModel::setCommand(const QString& key, int value, QString* error) {
    const QVector<int> keys = keyframes();
    if (mode_ != Mode::Animation || everyFrame_ || frame_ >= keys.size()) return false;
    const int at = keys[frame_];
    // Under "add" timing frames is a signed count over the steps.
    if (key == QLatin1String("frames") && boss().value(QStringLiteral("timing")).toString() == QLatin1String("add")) {
        if (value < -127 || value > 127) {
            if (error) *error = tr("Frames over steps run -127 to 127.");
            return false;
        }
        value &= 0xFF;
    }
    const QString label = run().labels[at];
    const int source = run().sources[at];
    if ((key == QLatin1String("pose") && (value < 0 || value >= poseRows().size())) ||
        (key == QLatin1String("frames") && (value < 0 || value > 0xFF || value == kSoundCommand)) ||
        (key == QLatin1String("steps") && (value < 1 || value > 0xFF))) {
        // A count of $80 would read as a sound; no steps divides by zero.
        if (error) *error = tr("Frames 0-255 but 128, steps 1-255, a pose the table holds.");
        return false;
    }
    return session_->edit(
        Doc::Bosses, tr("Change boss animation"),
        [&](QJsonObject& document) {
            QJsonArray sequences = document.value(QStringLiteral("sequences")).toArray();
            const int index = indexOfLabel(sequences, label);
            QJsonObject sequence = sequences.at(index).toObject();
            QJsonArray commands = sequence.value(QStringLiteral("commands")).toArray();
            QJsonObject command = commands.at(source).toObject();
            command.insert(key, value);
            commands.replace(source, command);
            sequence.insert(QStringLiteral("commands"), commands);
            sequences.replace(index, sequence);
            document.insert(QStringLiteral("sequences"), sequences);
        },
        error);
}

bool BossesModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key.startsWith(QLatin1String("channel:")))
        return setPoseChannel(editedPose(), key.mid(8).toInt(), value.toInt(), error);
    if (key == QLatin1String("pose") || key == QLatin1String("frames") || key == QLatin1String("steps"))
        return setCommand(key, value.toInt(), error);
    return false;
}

QString BossesModel::animationTitle(const QString& label, const QString& boss) {
    QString name = label;
    if (name.startsWith(QLatin1String("Boss_"))) name = name.mid(5);
    QString squeezed = boss;
    squeezed.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    if (!squeezed.isEmpty() && name.startsWith(squeezed, Qt::CaseInsensitive)) name = name.mid(squeezed.size());
    for (const char* tail : {"PoseCommands", "PoseScript", "Commands", "Script"}) {
        if (name.endsWith(QLatin1String(tail)) && name.size() > int(qstrlen(tail))) {
            name.chop(int(qstrlen(tail)));
            break;
        }
    }
    // Words from the capitals; a lone capital (A, B) stays one.
    QStringList words;
    QRegularExpressionMatchIterator found =
        QRegularExpression(QStringLiteral("[A-Z][a-z0-9]*|[a-z0-9]+")).globalMatch(name);
    while (found.hasNext()) words << found.next().captured();
    if (words.isEmpty()) return label;
    for (int index = 1; index < words.size(); ++index)
        if (words[index].size() > 1) words[index] = words[index].toLower();
    return words.join(QLatin1Char(' '));
}

}  // namespace rl::alien_soldier
