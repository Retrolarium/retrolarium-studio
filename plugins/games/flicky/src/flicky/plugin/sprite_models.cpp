#include "flicky/plugin/sprite_models.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "flicky/plugin/objects.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::flicky {

namespace {

constexpr int kOffsetMin = -128;
constexpr int kOffsetMax = 127;

QString roundLabel(int round) {
    return QStringLiteral("Round %1").arg(round + 1, 2, 10, QLatin1Char('0'));
}

QStringList roundChoices() {
    QStringList choices;
    for (int round = 0; round < objects::kRounds; ++round) {
        choices << roundLabel(round);
    }
    return choices;
}

// The pieces of one mapping around `origin`, in canvas pixels.
QRect mappingExtent(const FlickySequencesDocument& sequences, const QString& id, QPoint origin) {
    QRect extent;
    for (const QJsonValue& value : sequences.mapping(id).value(QStringLiteral("pieces")).toArray()) {
        const QJsonObject p = value.toObject();
        extent |= QRect(origin.x() + p.value(QStringLiteral("x")).toInt(),
                        origin.y() + p.value(QStringLiteral("y")).toInt(),
                        p.value(QStringLiteral("width")).toInt() * 8, p.value(QStringLiteral("height")).toInt() * 8);
    }
    return extent;
}

// A sprite's extent with room around it and the origin in view, kept on the
// canvas.
QRect focusAround(QRect extent, QPoint origin, QSize canvas) {
    extent |= QRect(origin, QSize(1, 1));
    return extent.adjusted(-12, -12, 12, 12) & QRect(QPoint(0, 0), canvas);
}

QImage failure(const QString& message) {
    QImage image(256, 224, QImage::Format_RGB32);
    image.fill(QColor(0x20, 0x16, 0x18));
    QPainter painter(&image);
    painter.setPen(QColor(0xff, 0xb4, 0xa8));
    painter.drawText(image.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, message);
    return image;
}

QString hex(int value, int digits) {
    return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

sdk::Field integerField(const QString& key, const QString& label, int value, int minimum, int maximum,
                        const QString& hint = QString()) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Integer;
    field.value = value;
    field.minimum = minimum;
    field.maximum = maximum;
    field.hint = hint;
    field.monospace = true;
    return field;
}

sdk::Field choiceField(const QString& key, const QString& label, const QStringList& choices,
                       const QString& value) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.type = sdk::Field::Type::Choice;
    field.choices = choices;
    field.value = value;
    return field;
}

const QStringList kYesNo{QStringLiteral("No"), QStringLiteral("Yes")};

}  // namespace

// ---------------------------------------------------------------- mapping

MappingModel::MappingModel(FlickySession* session, const QString& id)
    : CompositionModel(session), session_(session), id_(id) {
    connect(session_, &FlickySession::sequencesChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject MappingModel::piece(int index) const {
    return session_->sequences().mapping(id_).value(QStringLiteral("pieces")).toArray().at(index).toObject();
}

QImage MappingModel::render() const {
    try {
        return session_->renderer().renderMappingPreview(id_, session_->paletteRound() + 1);
    } catch (const std::exception& e) {
        return failure(QString::fromUtf8(e.what()));
    }
}

QRect MappingModel::focusRect() const {
    try {
        return focusAround(mappingExtent(session_->sequences(), id_, origin()), origin(), canvasSize());
    } catch (const std::exception&) {
        return QRect(QPoint(0, 0), canvasSize());
    }
}

QVector<sdk::CompositionPiece> MappingModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const QJsonArray pieces = session_->sequences().mapping(id_).value(QStringLiteral("pieces")).toArray();
    for (int i = 0; i < pieces.size(); ++i) {
        const QJsonObject p = pieces.at(i).toObject();
        sdk::CompositionPiece piece;
        piece.id = QString::number(i);
        piece.label = QStringLiteral("Piece %1 · %2").arg(i).arg(hex(p.value(QStringLiteral("tile")).toInt(), 3));
        piece.pixels = QRect(origin().x() + p.value(QStringLiteral("x")).toInt(),
                             origin().y() + p.value(QStringLiteral("y")).toInt(),
                             p.value(QStringLiteral("width")).toInt() * 8, p.value(QStringLiteral("height")).toInt() * 8);
        result.append(piece);
    }
    return result;
}

QString MappingModel::selectedPiece() const {
    return selected_ >= 0 ? QString::number(selected_) : QString();
}

void MappingModel::selectPiece(const QString& id) {
    bool ok = false;
    const int index = id.toInt(&ok);
    const int next = ok ? index : -1;
    if (next != selected_) {
        selected_ = next;
        emit selectionChanged();
    }
}

bool MappingModel::setPiece(int index, const QJsonObject& piece, QString* error) {
    const QString id = id_;
    return session_->editSequences(QStringLiteral("Edit %1 piece %2").arg(id).arg(index),
                                   [id, index, piece](FlickySequencesDocument& q) { q.setMappingPiece(id, index, piece); },
                                   error);
}

void MappingModel::beginDrag(const QString& id) {
    selectPiece(id);
    before_ = session_->sequences();
    beforeRevision_ = session_->sequencesRevision();
    dragStart_ = piece(selected_);
    changed_ = false;
}

bool MappingModel::dragPiece(const QString& id, QPoint offset) {
    if (!before_ || id.toInt() != selected_) {
        return false;
    }
    QJsonObject p = dragStart_;
    const int width = p.value(QStringLiteral("width")).toInt() * 8;
    // Both the piece and its mirror must stay within the signed byte range.
    const int minX = std::max(kOffsetMin, -kOffsetMax - width);
    const int maxX = std::min(kOffsetMax, -kOffsetMin - width);
    const int x = std::clamp(dragStart_.value(QStringLiteral("x")).toInt() + offset.x(), minX, maxX);
    const int y = std::clamp(dragStart_.value(QStringLiteral("y")).toInt() + offset.y(), kOffsetMin, kOffsetMax);
    if (x == piece(selected_).value(QStringLiteral("x")).toInt() && y == piece(selected_).value(QStringLiteral("y")).toInt()) {
        return false;
    }
    p.insert(QStringLiteral("x"), x);
    p.insert(QStringLiteral("flipped_x"), -x - width);
    p.insert(QStringLiteral("y"), y);
    try {
        session_->sequencesLive().setMappingPiece(id_, selected_, p);
    } catch (const std::exception&) {
        return false;
    }
    changed_ = true;
    session_->touchSequences();
    return true;
}

void MappingModel::endDrag() {
    if (before_ && changed_) {
        session_->commitSequences(*before_, beforeRevision_, QStringLiteral("Move %1 piece %2").arg(id_).arg(selected_));
    }
    before_.reset();
    changed_ = false;
}

QVector<sdk::ViewOption> MappingModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.value = roundLabel(session_->paletteRound());
    palette.choices = roundChoices();
    return {palette};
}

void MappingModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        session_->setPaletteRound(value.section(QLatin1Char(' '), 1).toInt() - 1);
        emit contentChanged();
        emit toolsChanged();
    }
}

sdk::Inspection MappingModel::inspect() const {
    const QJsonObject mapping = session_->sequences().mapping(id_);
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("flicky.animations.v1");
    if (selected_ < 0) {
        inspection.kicker = QStringLiteral("Sprite mapping");
        inspection.title = id_;
        inspection.fields = {integerField(QStringLiteral("render_flags"), QStringLiteral("Render flags"),
                                          mapping.value(QStringLiteral("render_flags")).toInt(), 0, 255, QStringLiteral("0–255"))};
        sdk::Field count;
        count.key = QStringLiteral("pieces");
        count.label = QStringLiteral("Pieces");
        count.value = QString::number(mapping.value(QStringLiteral("pieces")).toArray().size());
        inspection.fields << count;
        inspection.note = QStringLiteral("Pick a piece to edit it. Pieces are placed around the sprite "
                                         "origin (the crosshair); the palette comes from the chosen round.");
        inspection.engine = {{QStringLiteral("Mapping"), id_},
                             {QStringLiteral("Source"), mapping.value(QStringLiteral("source")).toString()}};
        return inspection;
    }
    const QJsonObject p = piece(selected_);
    inspection.kicker = QStringLiteral("Sprite piece");
    inspection.title = QStringLiteral("Piece %1").arg(selected_);
    const QStringList sizes{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")};
    sdk::Field flipped;
    flipped.key = QStringLiteral("flipped_x");
    flipped.label = QStringLiteral("Mirrored X");
    flipped.value = p.value(QStringLiteral("flipped_x")).toInt();
    flipped.hint = QStringLiteral("auto");
    flipped.monospace = true;
    inspection.fields = {
        integerField(QStringLiteral("x"), QStringLiteral("X"), p.value(QStringLiteral("x")).toInt(), kOffsetMin, kOffsetMax, QStringLiteral("px")),
        integerField(QStringLiteral("y"), QStringLiteral("Y"), p.value(QStringLiteral("y")).toInt(), kOffsetMin, kOffsetMax, QStringLiteral("px")),
        flipped,
        choiceField(QStringLiteral("width"), QStringLiteral("Width"), sizes, QString::number(p.value(QStringLiteral("width")).toInt())),
        choiceField(QStringLiteral("height"), QStringLiteral("Height"), sizes, QString::number(p.value(QStringLiteral("height")).toInt())),
        integerField(QStringLiteral("tile"), QStringLiteral("Tile"), p.value(QStringLiteral("tile")).toInt(), 0, 0x7FF, QStringLiteral("VRAM")),
        choiceField(QStringLiteral("palette"), QStringLiteral("Palette line"), {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")},
                    QString::number(p.value(QStringLiteral("palette")).toInt())),
        choiceField(QStringLiteral("priority"), QStringLiteral("Priority"), kYesNo, p.value(QStringLiteral("priority")).toBool() ? kYesNo.at(1) : kYesNo.at(0)),
        choiceField(QStringLiteral("hflip"), QStringLiteral("X flip"), kYesNo, p.value(QStringLiteral("hflip")).toBool() ? kYesNo.at(1) : kYesNo.at(0)),
        choiceField(QStringLiteral("vflip"), QStringLiteral("Y flip"), kYesNo, p.value(QStringLiteral("vflip")).toBool() ? kYesNo.at(1) : kYesNo.at(0)),
    };
    inspection.note = QStringLiteral("Drag the piece or nudge it with the arrow keys. The mirrored X that "
                                     "flipped sprites use follows automatically.");
    inspection.engine = {{QStringLiteral("Mapping"), id_},
                         {QStringLiteral("Piece"), QStringLiteral("%1 of %2").arg(selected_ + 1).arg(mapping.value(QStringLiteral("pieces")).toArray().size())},
                         {QStringLiteral("Tile word"), hex(p.value(QStringLiteral("tile")).toInt(), 3)}};
    return inspection;
}

bool MappingModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("render_flags")) {
        const QString id = id_;
        const int flags = value.toInt();
        return session_->editSequences(QStringLiteral("Edit %1 render flags").arg(id),
                                       [id, flags](FlickySequencesDocument& q) { q.setMappingRenderFlags(id, flags); },
                                       error);
    }
    if (selected_ < 0) {
        return CompositionModel::setField(key, value, error);
    }
    QJsonObject p = piece(selected_);
    if (key == QLatin1String("x") || key == QLatin1String("width")) {
        const int width = (key == QLatin1String("width") ? value.toString().toInt() : p.value(QStringLiteral("width")).toInt()) * 8;
        const int x = key == QLatin1String("x") ? value.toInt() : p.value(QStringLiteral("x")).toInt();
        if (-x - width < kOffsetMin || -x - width > kOffsetMax) {
            if (error) *error = QStringLiteral("The mirrored position would leave the -128..127 range.");
            return false;
        }
        p.insert(QStringLiteral("x"), x);
        p.insert(QStringLiteral("width"), width / 8);
        p.insert(QStringLiteral("flipped_x"), -x - width);
    } else if (key == QLatin1String("y") || key == QLatin1String("tile")) {
        p.insert(key, value.toInt());
    } else if (key == QLatin1String("height") || key == QLatin1String("palette")) {
        p.insert(key, value.toString().toInt());
    } else if (key == QLatin1String("priority") || key == QLatin1String("hflip") || key == QLatin1String("vflip")) {
        p.insert(key, value.toString() == QLatin1String("Yes"));
    } else {
        return CompositionModel::setField(key, value, error);
    }
    return setPiece(selected_, p, error);
}

QString MappingModel::statusText() const {
    if (selected_ < 0) {
        return QStringLiteral("%1 · %2 pieces").arg(id_).arg(pieces().size());
    }
    const QJsonObject p = piece(selected_);
    return QStringLiteral("%1 · piece %2 at %3, %4")
        .arg(id_)
        .arg(selected_)
        .arg(p.value(QStringLiteral("x")).toInt())
        .arg(p.value(QStringLiteral("y")).toInt());
}

// ---------------------------------------------------------------- animation

AnimationModel::AnimationModel(FlickySession* session, const QString& id)
    : CompositionModel(session), session_(session), id_(id) {
    connect(session_, &FlickySession::sequencesChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject AnimationModel::animation() const {
    return session_->sequences().animation(id_);
}

QVector<sdk::CompositionFrame> AnimationModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QJsonObject a = animation();
    const int delay = a.value(QStringLiteral("delay")).toInt();
    for (const QJsonValue& value : a.value(QStringLiteral("frames")).toArray()) {
        sdk::CompositionFrame frame;
        frame.id = value.toString();
        frame.label = value.toString();
        frame.detail = QStringLiteral("%1 tick%2").arg(delay + 1).arg(delay == 0 ? QString() : QStringLiteral("s"));
        result.append(frame);
    }
    return result;
}

void AnimationModel::setCurrentFrame(int frame) {
    const int count = frames().size();
    frame = std::clamp(frame, 0, std::max(0, count - 1));
    if (frame != frame_) {
        frame_ = frame;
        emit selectionChanged();
        emit contentChanged();
    }
}

int AnimationModel::frameDurationMs(int) const {
    // One step every delay+1 frames at 60 Hz.
    return std::max(16, (animation().value(QStringLiteral("delay")).toInt() + 1) * 1000 / 60);
}

QRect AnimationModel::focusRect() const {
    // Every frame at once, so the view stays put while the animation plays.
    const QJsonObject a = animation();
    const bool tilemap = a.value(QStringLiteral("kind")).toString() == QLatin1String("tilemap");
    QRect extent;
    try {
        for (const QJsonValue& frame : a.value(QStringLiteral("frames")).toArray()) {
            // Tile-word frames are 3x1 or 3x3 tiles centred on the screen.
            extent |= tilemap ? QRect(origin() - QPoint(12, 12), QSize(24, 24))
                              : mappingExtent(session_->sequences(), frame.toString(), origin());
        }
    } catch (const std::exception&) {
        return QRect(QPoint(0, 0), canvasSize());
    }
    return focusAround(extent, origin(), canvasSize());
}

QImage AnimationModel::render() const {
    const QJsonObject a = animation();
    const QString frame = a.value(QStringLiteral("frames")).toArray().at(frame_).toString();
    try {
        if (a.value(QStringLiteral("kind")).toString() == QLatin1String("tilemap")) {
            return session_->renderer().renderTilemapFramePreview(frame, session_->paletteRound() + 1);
        }
        return session_->renderer().renderMappingPreview(frame, session_->paletteRound() + 1);
    } catch (const std::exception& e) {
        return failure(QString::fromUtf8(e.what()));
    }
}

bool AnimationModel::moveFrame(int from, int to, QString* error) {
    QJsonObject a = animation();
    QJsonArray list = a.value(QStringLiteral("frames")).toArray();
    if (from < 0 || to < 0 || from >= list.size() || to >= list.size()) {
        return false;
    }
    const QJsonValue moved = list.at(from);
    list.replace(from, list.at(to));
    list.replace(to, moved);
    a.insert(QStringLiteral("frames"), list);
    const QString id = id_;
    if (!session_->editSequences(QStringLiteral("Reorder %1").arg(id),
                                 [id, a](FlickySequencesDocument& q) { q.replaceAnimation(id, a); }, error)) {
        return false;
    }
    setCurrentFrame(to);
    return true;
}

QVector<sdk::ViewOption> AnimationModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Palette");
    palette.value = roundLabel(session_->paletteRound());
    palette.choices = roundChoices();
    return {palette};
}

void AnimationModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        session_->setPaletteRound(value.section(QLatin1Char(' '), 1).toInt() - 1);
        emit contentChanged();
        emit toolsChanged();
    }
}

sdk::Inspection AnimationModel::inspect() const {
    const QJsonObject a = animation();
    const QJsonArray list = a.value(QStringLiteral("frames")).toArray();
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Animation");
    inspection.title = id_;
    sdk::Field kind;
    kind.key = QStringLiteral("kind");
    kind.label = QStringLiteral("Kind");
    kind.value = a.value(QStringLiteral("kind")).toString();
    sdk::Field frame;
    frame.key = QStringLiteral("frame");
    frame.label = QStringLiteral("Frame %1").arg(frame_ + 1);
    frame.type = sdk::Field::Type::Choice;
    frame.choices = session_->sequences().allowedFrames(a.value(QStringLiteral("kind")).toString());
    frame.value = list.at(frame_).toString();
    inspection.fields = {kind,
                         integerField(QStringLiteral("delay"), QStringLiteral("Delay"), a.value(QStringLiteral("delay")).toInt(), 0, 255,
                                      QStringLiteral("frames")),
                         frame};
    inspection.note = QStringLiteral("Each frame shows for delay + 1 video frames. Reorder frames with "
                                     "Earlier / Later; the frame count is fixed by the game.");
    inspection.engineTag = QStringLiteral("flicky.animations.v1");
    inspection.engine = {{QStringLiteral("Animation"), id_},
                         {QStringLiteral("Frames"), QString::number(list.size())},
                         {QStringLiteral("Source"), a.value(QStringLiteral("source")).toString()}};
    return inspection;
}

bool AnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    const QString id = id_;
    if (key == QLatin1String("delay")) {
        const int delay = value.toInt();
        return session_->editSequences(QStringLiteral("Edit %1 delay").arg(id),
                                       [id, delay](FlickySequencesDocument& q) { q.setAnimationDelay(id, delay); },
                                       error);
    }
    if (key == QLatin1String("frame")) {
        const int index = frame_;
        const QString frame = value.toString();
        return session_->editSequences(QStringLiteral("Edit %1 frame %2").arg(id).arg(index + 1),
                                       [id, index, frame](FlickySequencesDocument& q) {
                                           q.setAnimationFrame(id, index, frame);
                                       },
                                       error);
    }
    return CompositionModel::setField(key, value, error);
}

QString AnimationModel::statusText() const {
    return QStringLiteral("%1 · frame %2 of %3").arg(id_).arg(frame_ + 1).arg(frames().size());
}

// ---------------------------------------------------------------- bonus motion

BonusMotionModel::BonusMotionModel(FlickySession* session, const QString& id)
    : TableModel(session), session_(session), id_(id) {
    connect(session_, &FlickySession::sequencesChanged, this, &sdk::EditorModel::contentChanged);
}

QJsonObject BonusMotionModel::record() const {
    return session_->sequences().bonusMotion(id_);
}

int BonusMotionModel::word(int row) const {
    return record().value(QStringLiteral("words")).toArray().at(row).toInt();
}

bool BonusMotionModel::isLocked(int row) const {
    // A trajectory ends with the $FFFF sentinel the game scans for.
    return record().value(QStringLiteral("kind")).toString() == QLatin1String("trajectory") && row == rowCount() - 1;
}

QString BonusMotionModel::meaning(int row) const {
    // Decoding follows the game's bonus code (see the source's bonus tables).
    const QString kind = record().value(QStringLiteral("kind")).toString();
    const int value = word(row);
    const auto signedByte = [](int byte) { return byte < 0x80 ? byte : byte - 0x100; };
    if (kind == QLatin1String("delay")) {
        return QStringLiteral("Chick %1 waits %2 frames before moving").arg(row).arg(value);
    }
    if (kind == QLatin1String("velocity")) {
        return QStringLiteral("Chick %1 flies at X %2/16, Y %3/16 px per frame")
            .arg(row)
            .arg(signedByte(value >> 8))
            .arg(signedByte(value & 0xFF));
    }
    if (kind == QLatin1String("trajectory_index")) {
        return QStringLiteral("Chicks %1 and %2 follow trajectories %3 and %4")
            .arg(row * 2)
            .arg(row * 2 + 1)
            .arg(value >> 8)
            .arg(value & 0xFF);
    }
    if (isLocked(row)) {
        return QStringLiteral("End of trajectory ($FFFF, fixed)");
    }
    if (row % 2 == 0) {
        return QStringLiteral("Segment %1 runs for %2 frames").arg(row / 2).arg(value);
    }
    return QStringLiteral("Segment %1: X acceleration %2/256, target X speed %3/16")
        .arg(row / 2)
        .arg(signedByte(value >> 8))
        .arg(signedByte(value & 0xFF));
}

QVector<sdk::TableColumn> BonusMotionModel::columns() const {
    sdk::TableColumn index;
    index.key = QStringLiteral("index");
    index.label = QStringLiteral("Slot");
    index.width = 90;
    index.muted = true;
    sdk::TableColumn value;
    value.key = QStringLiteral("word");
    value.label = QStringLiteral("Word");
    value.width = 140;
    value.monospace = true;
    value.editable = true;
    sdk::TableColumn meaningColumn;
    meaningColumn.key = QStringLiteral("meaning");
    meaningColumn.label = QStringLiteral("Meaning");
    meaningColumn.muted = true;
    return {index, value, meaningColumn};
}

int BonusMotionModel::rowCount() const {
    return record().value(QStringLiteral("words")).toArray().size();
}

QString BonusMotionModel::cell(int row, int column) const {
    if (column == 0) return QString::number(row);
    if (column == 1) return hex(word(row), 4);
    return meaning(row);
}

bool BonusMotionModel::isRowDirty(int row) const {
    return word(row) != session_->savedSequences().bonusMotion(id_).value(QStringLiteral("words")).toArray().at(row).toInt();
}

void BonusMotionModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool BonusMotionModel::setWord(int row, const QString& text, QString* error) {
    if (isLocked(row)) {
        if (error) *error = QStringLiteral("The $FFFF end marker of a trajectory cannot change.");
        return false;
    }
    QString digits = text.trimmed();
    digits.remove(QLatin1Char('$'));
    bool ok = false;
    const int value = digits.toInt(&ok, 16);
    if (!ok || value < 0 || value > 0xFFFF) {
        if (error) *error = QStringLiteral("A word is up to four hex digits, e.g. $0120.");
        return false;
    }
    const QString id = id_;
    return session_->editSequences(QStringLiteral("Edit %1 slot %2").arg(id).arg(row),
                                   [id, row, value](FlickySequencesDocument& q) { q.setBonusMotionWord(id, row, value); },
                                   error);
}

bool BonusMotionModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column != 1) {
        return TableModel::setCell(row, column, value, error);
    }
    return setWord(row, value, error);
}

bool BonusMotionModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("word")) {
        return setWord(row_, value.toString(), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Inspection BonusMotionModel::inspect() const {
    const QJsonObject r = record();
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Bonus motion");
    inspection.title = QStringLiteral("%1 · slot %2").arg(id_).arg(row_);
    sdk::Field value;
    value.key = QStringLiteral("word");
    value.label = QStringLiteral("Word");
    value.type = isLocked(row_) ? sdk::Field::Type::ReadOnly : sdk::Field::Type::Text;
    value.value = hex(word(row_), 4);
    value.monospace = true;
    sdk::Field kind;
    kind.key = QStringLiteral("kind");
    kind.label = QStringLiteral("Table kind");
    kind.value = r.value(QStringLiteral("kind")).toString();
    QStringList rounds;
    for (const QJsonValue& round : r.value(QStringLiteral("rounds")).toArray()) {
        rounds << QString::number(round.toInt());
    }
    sdk::Field used;
    used.key = QStringLiteral("rounds");
    used.label = QStringLiteral("Rounds");
    used.value = rounds.isEmpty() ? QStringLiteral("all bonus rounds") : rounds.join(QStringLiteral(", "));
    inspection.fields = {value, kind, used};
    inspection.note = meaning(row_);
    inspection.engineTag = QStringLiteral("flicky.animations.v1");
    inspection.engine = {{QStringLiteral("Table"), id_},
                         {QStringLiteral("Words"), QString::number(rowCount())},
                         {QStringLiteral("Saved"),
                          hex(session_->savedSequences().bonusMotion(id_).value(QStringLiteral("words")).toArray().at(row_).toInt(), 4)}};
    return inspection;
}

QString BonusMotionModel::statusText() const {
    return QStringLiteral("%1 · slot %2 · %3").arg(id_).arg(row_).arg(hex(word(row_), 4));
}

}  // namespace rl::flicky
