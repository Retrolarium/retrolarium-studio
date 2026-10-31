#include "alien_soldier/plugin/bosses_model.hpp"

#include "alien_soldier/plugin/session.hpp"
#include "sdk/game_names.hpp"

#include <QJsonArray>
#include <QPainter>
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

// Boss previews show one still, not the union of an animation's frames.
// Transparent margins are equal on every side, regardless of the boss origin.
QRect visiblePixels(const QImage& image) {
    QRect area;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x])) area |= QRect(x, y, 1, 1);
    }
    return area;
}

QImage framed(const QImage& image) {
    const QRect ink = visiblePixels(image);
    if (ink.isEmpty()) return image;
    const QRect crop = ink.adjusted(-8, -8, 8, 8);
    QImage result(crop.size(), QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawImage(-crop.topLeft(), image);
    return result;
}

QJsonObject previewSpec(const QJsonObject& boss) {
    return sdk::gameCatalog("boss_palette_previews")
        .toObject()
        .value(QStringLiteral("bosses"))
        .toObject()
        .value(boss.value(QStringLiteral("id")).toString())
        .toObject();
}

}  // namespace

QString bossPreviewTitle(const QJsonObject& boss) {
    return previewSpec(boss).value(QStringLiteral("title")).toString(boss.value(QStringLiteral("title")).toString());
}

BossPreview makeBossPreview(AlienSoldierSession* session, const QJsonObject& boss, bool paletteSample) {
    QJsonObject spec = previewSpec(boss);
    if (paletteSample) {
        const QJsonObject overrides = spec.value(QStringLiteral("palette")).toObject();
        for (auto item = overrides.begin(); item != overrides.end(); ++item) spec.insert(item.key(), item.value());
    }
    const QJsonArray animations = boss.value(QStringLiteral("sequences")).toArray();
    if (spec.isEmpty() && !boss.value(QStringLiteral("poses")).isNull() && !animations.isEmpty()) {
        const QString id = boss.value(QStringLiteral("id")).toString();
        BossesModel model(session, QStringLiteral("anim:%1:%2").arg(id, animations.first().toString()));
        return {framed(model.render()), {}};
    }
    QVector<int> words;
    QJsonArray values = boss.value(QStringLiteral("neutral_pose")).toArray();
    const QJsonArray poses = session->doc(Doc::Bosses)
                                 .value(QStringLiteral("poses"))
                                 .toObject()
                                 .value(boss.value(QStringLiteral("poses")).toString())
                                 .toArray();
    const int pose = spec.value(QStringLiteral("pose")).toInt(-1);
    if (pose >= 0 && pose < poses.size()) values = poses.at(pose).toObject().value(QStringLiteral("values")).toArray();
    for (const QJsonValue& value : values) words.append(value.toInt() << 8);
    BossStance stance;
    stance.facing = boss.value(QStringLiteral("faces")).toInt(0x100);
    QVector<PlacedPart> parts =
        placeBoss(boss, session->doc(Doc::Bosses).value(QStringLiteral("cosine")).toArray(), words, stance);
    const SpriteContext context = bossContext(session->docs(), boss);
    QJsonObject alone = boss;
    alone.remove(QStringLiteral("body"));
    const auto pixels = [&](const PlacedPart& part) {
        const QRect rect = partRect(boss, part, session->docs());
        if (rect.isEmpty()) return QRect();
        QImage image(rect.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        drawBoss(image, -rect.topLeft(), alone, {part}, session->docs(), context);
        return visiblePixels(image).translated(rect.topLeft());
    };
    // Game-owned presentation changes affect this copy of the placements only.
    const QJsonObject positions = spec.value(QStringLiteral("part_positions")).toObject();
    const QJsonObject offsets = spec.value(QStringLiteral("part_offsets")).toObject();
    for (PlacedPart& part : parts) {
        const QString index = QString::number(part.index);
        if (positions.contains(index)) {
            const QJsonArray at = positions.value(index).toArray();
            part.at = QPoint(at.at(0).toInt(), at.at(1).toInt());
        }
        const QJsonArray delta = offsets.value(index).toArray();
        if (delta.size() == 2) part.at += QPoint(delta.at(0).toInt(), delta.at(1).toInt());
    }
    for (const QJsonValue& value : spec.value(QStringLiteral("chains")).toArray()) {
        const QJsonObject chain = value.toObject();
        const QJsonArray direction = chain.value(QStringLiteral("direction")).toArray();
        const QPoint step(direction.at(0).toInt(), direction.at(1).toInt());
        const bool horizontal = step.x() != 0;
        const int sign = horizontal ? step.x() : step.y();
        if (std::abs(sign) != 1 || (horizontal && step.y() != 0)) continue;
        const QRect anchor = pixels(parts.first());
        int cursor = horizontal ? (sign > 0 ? anchor.right() + 1 : anchor.left() - 1)
                                : (sign > 0 ? anchor.bottom() + 1 : anchor.top() - 1);
        for (const QJsonValue& index : chain.value(QStringLiteral("parts")).toArray()) {
            if (index.toInt(-1) < 0 || index.toInt() >= parts.size()) continue;
            PlacedPart& part = parts[index.toInt()];
            const QRect local = pixels(part).translated(-part.at);
            if (local.isEmpty()) continue;
            part.at = horizontal ? QPoint(cursor - (sign > 0 ? local.left() : local.right()), -local.center().y())
                                 : QPoint(-local.center().x(), cursor - (sign > 0 ? local.top() : local.bottom()));
            cursor += sign * (horizontal ? local.width() : local.height());
        }
    }
    const QJsonArray hidden = spec.value(QStringLiteral("hidden_parts")).toArray();
    QJsonObject viewBoss = boss;
    QJsonArray records = boss.value(QStringLiteral("parts")).toArray();
    for (PlacedPart& part : parts) {
        if (!hidden.contains(part.index)) continue;
        part.mapping.clear();
        QJsonObject record = records.at(part.index).toObject();
        record.insert(QStringLiteral("kind"), QStringLiteral("none"));
        records.replace(part.index, record);
        // Keep the indices stable for plane carriers and depth sorting.
    }
    viewBoss.insert(QStringLiteral("parts"), records);
    const int columns = spec.value(QStringLiteral("body_columns")).toInt();
    if (columns > 0) {
        QJsonObject body = viewBoss.value(QStringLiteral("body")).toObject();
        QJsonArray cells;
        for (const QJsonValue& value : body.value(QStringLiteral("cells")).toArray()) {
            const QJsonArray row = value.toArray();
            QJsonArray shortened;
            for (int column = 0; column < std::min(columns, row.size()); ++column) shortened.append(row.at(column));
            cells.append(shortened);
        }
        body.insert(QStringLiteral("cells"), cells);
        viewBoss.insert(QStringLiteral("body"), body);
    }
    QRect area = bodyRect(viewBoss);
    for (const PlacedPart& part : parts) area |= partRect(viewBoss, part, session->docs());
    if (area.isEmpty()) return {};
    area = area.adjusted(-8, -8, 8, 8);
    QImage image(area.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawBoss(image, -area.topLeft(), viewBoss, parts, session->docs(), context);
    const int scale = std::clamp(spec.value(QStringLiteral("pixel_scale")).toInt(1), 1, 8);
    if (scale > 1) image = image.scaled(image.size() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    BossPreview result;
    const QRect ink = visiblePixels(image);
    result.image = framed(image);
    const QPoint translation = -area.topLeft() * scale - ink.topLeft() + QPoint(8, 8);
    for (const PlacedPart& part : parts) {
        if (hidden.contains(part.index)) continue;
        const QRect bounds = pixels(part);
        if (bounds.isEmpty()) continue;
        sdk::CompositionPiece piece;
        piece.id = QString::number(part.index);
        piece.label = BossesModel::tr("Part %1").arg(part.index);
        piece.pixels = QRect(bounds.topLeft() * scale + translation, bounds.size() * scale) & result.image.rect();
        piece.movable = false;
        if (!piece.pixels.isEmpty()) result.pieces.append(piece);
    }
    return result;
}

BossesModel::BossesModel(AlienSoldierSession* session, const QString& document)
    : CompositionModel(session), session_(session) {
    const QStringList parts = document.split(QLatin1Char(':'));
    if (parts.value(0) == QLatin1String("anim") && parts.size() == 3) {
        mode_ = Mode::Animation;
        bossId_ = parts[1];
        label_ = parts[2];
    } else if (parts.value(0) == QLatin1String("boss") && parts.size() == 2) {
        bossId_ = parts[1];
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Bosses || doc == Doc::Mappings || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes) {
            if (doc != Doc::Bosses && doc != Doc::Mappings) contextReady_ = false;
            previewReady_ = false;
            emit contentChanged();
        }
    });
}

QJsonObject BossesModel::boss() const { return bossById(session_->docs(), bossId_); }

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
        // Round again, the first keyframe turns from the last pose.
        looped_ = PoseFrames();
        if (run_.loops && !stepped_.channels.isEmpty()) {
            QVector<int> end;
            for (int word : stepped_.channels.last()) end.append(word >> 8);
            looped_ = stepPoses(boss(), poseRows(), run_, end, kMaxFrames);
        }
    }
    return stepped_;
}

BossesModel::Way BossesModel::way(int frame) const {
    const PoseFrames& frames = stepped();
    const QVector<int> keys = keyframes();
    Way found;
    if (frame < 0 || frame >= keys.size()) return found;
    if (frame == 0 && !looped_.channels.isEmpty()) {
        found.frames = &looped_;
        found.last = looped_.ends.value(keys[0], -1);
        return found;
    }
    found.frames = &frames;
    found.first = frame > 0 ? frames.ends.value(keys[frame - 1], -1) + 1 : 0;
    found.last = frames.ends.value(keys[frame], -1);
    return found;
}

QVector<int> BossesModel::keyframes() const {
    QVector<int> found;
    for (int index = 0; index < run().commands.size(); ++index)
        if (!run().commands[index].sound) found.append(index);
    return found;
}

QVector<int> BossesModel::channelsAt(int frame) const {
    const PoseFrames& frames = stepped();
    if (frames.channels.isEmpty()) return {};
    if (everyFrame_) return frames.channels.value(std::clamp(frame, 0, int(frames.channels.size()) - 1));
    const QVector<int> keys = keyframes();
    if (frame < 0 || frame >= keys.size()) return frames.channels.last();
    const int end = frames.ends.value(keys[frame], -1);
    if (end < 0) return frames.channels.last();
    // Playing: the frame on the way to the keyframe.
    const Way path = way(frame);
    if (playStep_ >= 0 && path.frames && path.last >= path.first)
        return path.frames->channels.value(std::min(path.last, path.first + playStep_));
    return frames.channels[end];
}

int BossesModel::editedPose() const {
    if (mode_ != Mode::Animation || everyFrame_) return -1;
    const QVector<int> keys = keyframes();
    return frame_ >= 0 && frame_ < keys.size() ? run().commands[keys[frame_]].value : -1;
}

QVector<PlacedPart> BossesModel::placed(int frame) const {
    const QJsonArray cosine = session_->doc(Doc::Bosses).value(QStringLiteral("cosine")).toArray();
    return placeBoss(boss(), cosine, channelsAt(frame), stance());
}

BossStance BossesModel::stance() const {
    // A boss whose code never turns it (the mounted boss, its body built facing
    // one way) stands as it is built.
    BossStance kept = stance_;
    const QJsonValue faces = boss().value(QStringLiteral("faces"));
    if (faces.isDouble()) kept.facing = faces.toInt();
    return kept;
}

void BossesModel::refresh() {
    focus_ = QRect();
    drawsPixels_.clear();
    emit toolsChanged();
    emit contentChanged();
}

QString BossesModel::title() const {
    const QString name =
        mode_ == Mode::Still ? bossPreviewTitle(boss()) : boss().value(QStringLiteral("title")).toString();
    return mode_ == Mode::Still ? name : tr("%1 · %2").arg(name, animationTitle(label_, name));
}

QString BossesModel::readOnlyReason() const {
    return tr("The assembled boss is shown in full. Select a part to inspect it.");
}

QString BossesModel::statusText() const {
    const QJsonObject record = boss();
    return tr("%1 · %2 parts · %3 channels")
        .arg(title())
        .arg(record.value(QStringLiteral("parts")).toArray().size())
        .arg(record.value(QStringLiteral("channels")).toInt());
}

QVector<sdk::ViewOption> BossesModel::viewOptions() const {
    if (mode_ == Mode::Still) return {};
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
    sdk::ViewOption steps;
    steps.key = QStringLiteral("steps");
    steps.label = tr("Shows");
    steps.choices = QStringList{tr("Keyframes"), tr("Every frame")};
    steps.value = steps.choices[everyFrame_ ? 1 : 0];
    options.append(steps);
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

const BossPreview& BossesModel::preview() const {
    if (!previewReady_) {
        preview_ = makeBossPreview(session_, boss());
        previewReady_ = true;
    }
    return preview_;
}

QSize BossesModel::canvasSize() const {
    if (mode_ == Mode::Still) return preview().image.size();
    // Every paint asks; the frames are laid out again only when the mappings
    // or the view change (a pose dragged further grows it when the view
    // opens again: laying out every frame on each step of a drag is slow).
    const QString key = QStringLiteral("%1/%2/%3/%4")
                            .arg(session_->revision(Doc::Mappings))
                            .arg(everyFrame_)
                            .arg(stance_.facing)
                            .arg(stance_.side);
    if (key == canvasKey_) return canvas_;
    canvasKey_ = key;
    const QJsonObject record = boss();
    QRect area = bodyRect(record);
    const int count = int(frames().size());
    for (int frame = 0; frame < std::max(1, count); ++frame) {
        for (const PlacedPart& part : placed(frame)) {
            if (drawsPixels(record, part)) area |= partRect(record, part, session_->docs());
        }
    }
    const int across = std::max(std::abs(area.left()), std::abs(area.right() + 1));
    const int down = std::max(std::abs(area.top()), std::abs(area.bottom() + 1)) + 32;
    const QSize needed(std::max(512, 2 * across + 64), std::max(512, 2 * down + 64));
    canvas_ = canvas_.expandedTo(needed);
    return canvas_;
}

bool BossesModel::drawsPixels(const QJsonObject& record, const PlacedPart& part) const {
    const QRect rect = partRect(record, part, session_->docs());
    if (rect.isEmpty()) return false;
    const QString key = QStringLiteral("%1/%2/%3").arg(part.index).arg(part.mapping).arg(part.attribute);
    const auto known = drawsPixels_.constFind(key);
    if (known != drawsPixels_.constEnd()) return known.value();
    if (!contextReady_) {
        context_ = bossContext(session_->docs(), record);
        contextReady_ = true;
    }
    // The part alone: no body plane, no scene behind it.
    QJsonObject alone = record;
    alone.remove(QStringLiteral("body"));
    alone.remove(QStringLiteral("scene"));
    QImage image(rect.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawBoss(image, -rect.topLeft(), alone, {part}, session_->docs(), context_);
    bool any = false;
    for (int y = 0; y < image.height() && !any; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width() && !any; ++x) any = qAlpha(line[x]) != 0;
    }
    drawsPixels_.insert(key, any);
    return any;
}

QPoint BossesModel::origin() const {
    if (mode_ == Mode::Still) return {-1, -1};
    const QSize size = canvasSize();
    return {size.width() / 2, size.height() / 2 + 32};
}

QImage BossesModel::render() const {
    if (mode_ == Mode::Still) return preview().image;
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
    if (mode_ == Mode::Still) return preview().image.rect();
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
    if (mode_ == Mode::Still) return preview().pieces;
    QVector<sdk::CompositionPiece> result;
    const QJsonObject record = boss();
    if (record.isEmpty()) return result;
    const QJsonArray joints = record.value(QStringLiteral("joints")).toArray();
    const bool editable = editedPose() >= 0;
    for (const PlacedPart& part : placed(frame_)) {
        const QRect rect = partRect(record, part, session_->docs());
        // A record that draws nothing (an empty mapping, a part the code
        // shows later) gets no outline.
        if (!drawsPixels(record, part)) continue;
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
    if (mode_ == Mode::Still) {
        // The same assembled reference image used by Palettes.
        sdk::CompositionFrame frame;
        frame.id = QStringLiteral("0");
        frame.label = tr("Preview");
        result.append(frame);
        return result;
    }
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
    if (mode_ == Mode::Still || everyFrame_) return kFrameMs;
    return playSteps(frame) * kFrameMs;
}

int BossesModel::playSteps(int frame) const {
    if (mode_ != Mode::Animation || everyFrame_) return 1;
    const Way path = way(frame);
    return std::max(1, path.last - path.first + 1);
}

void BossesModel::setPlayStep(int step) {
    if (step == playStep_) return;
    playStep_ = step;
    emit contentChanged();
}

sdk::Inspection BossesModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject record = boss();
    if (record.isEmpty() || (mode_ == Mode::Still && piece_.isEmpty())) {
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
        inspection.note = mode_ == Mode::Still
                              ? readOnlyReason()
                              : tr("A part sits at the end of its bone from its parent, turned by the channels its "
                                   "joint adds up. Drag it to turn its own joint in the pose shown.");
        return inspection;
    }
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
        tr("A keyframe steps towards its pose by 1/steps of the way each frame, for frames + 1 frames, from "
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
    for (const char* tail : {"Poses", "PoseCommands", "PoseScript", "Commands", "Script"}) {
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
