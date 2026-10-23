#include "alien_soldier/plugin/animations_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

constexpr int kLastPiece = 0x8000;

int widthOf(int size) { return ((size >> 2) & 3) + 1; }
int heightOf(int size) { return (size & 3) + 1; }

// The tiles of an uncompressed art payload's first block (a dynamic piece's art).
QStringList artTiles(const QJsonObject& tiles, const QString& label) {
    const QJsonArray payloads = tiles.value(QStringLiteral("payloads")).toArray();
    const QJsonObject payload = payloads.at(indexOfLabel(payloads, label)).toObject();
    QStringList result;
    for (const QJsonValue& tile :
         payload.value(QStringLiteral("blocks")).toArray().at(0).toObject().value(QStringLiteral("tiles")).toArray()) {
        result << tile.toString();
    }
    return result;
}

}  // namespace

AnimationsModel::AnimationsModel(AlienSoldierSession* session, const QString& script)
    : CompositionModel(session), session_(session), script_(script) {
    paletteList_ = session_->doc(Doc::Stages)
                       .value(QStringLiteral("scenes"))
                       .toArray()
                       .at(0)
                       .toObject()
                       .value(QStringLiteral("palette_list"))
                       .toString();
    // The first entry that is a frame.
    for (int index = 0; index < frames().size(); ++index) {
        if (entry(index).value(QStringLiteral("duration")).toInt() != 0) {
            frame_ = index;
            break;
        }
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Animations || doc == Doc::Mappings || doc == Doc::Tiles || doc == Doc::Palettes)
            emit contentChanged();
    });
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

QPair<int, int> AnimationsModel::mappingOf(int frame) const {
    const QJsonObject item = entry(frame);
    if (item.value(QStringLiteral("duration")).toInt() == 0) return {-1, -1};
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    return {indexOfLabel(blocks, item.value(QStringLiteral("target")).toString()), 0};
}

QJsonObject AnimationsModel::pieceObject(int index) const {
    const QPair<int, int> mapping = mappingOf(frame_);
    if (mapping.first < 0) return {};
    return session_->doc(Doc::Mappings)
        .value(QStringLiteral("blocks"))
        .toArray()
        .at(mapping.first)
        .toObject()
        .value(QStringLiteral("mappings"))
        .toArray()
        .at(mapping.second)
        .toObject()
        .value(QStringLiteral("pieces"))
        .toArray()
        .at(index)
        .toObject();
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
        frame.detail = duration ? tr("%n calls", nullptr, duration & 0x7F) : tr("jump");
        result.append(frame);
    }
    return result;
}

void AnimationsModel::setCurrentFrame(int frame) {
    frame_ = std::clamp(frame, 0, std::max(0, frames().size() - 1));
    piece_.clear();
    emit selectionChanged();
    emit contentChanged();
}

int AnimationsModel::frameDurationMs(int frame) const {
    const int duration = entry(frame).value(QStringLiteral("duration")).toInt() & 0x7F;
    return duration ? duration * 1000 / 60 : 0;
}

QVector<sdk::CompositionPiece> AnimationsModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    for (int index = 0;; ++index) {
        const QJsonObject piece = pieceObject(index);
        if (piece.isEmpty()) break;
        const int size = piece.value(QStringLiteral("size")).toInt();
        sdk::CompositionPiece item;
        item.id = QString::number(index);
        item.label = tr("Piece %1").arg(index + 1);
        item.pixels =
            QRect(origin() + QPoint(piece.value(QStringLiteral("x")).toInt(), piece.value(QStringLiteral("y")).toInt()),
                  QSize(widthOf(size) * 8, heightOf(size) * 8));
        result.append(item);
        if (piece.value(QStringLiteral("attributes")).toInt() & kLastPiece) break;
    }
    return result;
}

QRect AnimationsModel::focusRect() const {
    QRect area;
    for (const sdk::CompositionPiece& piece : pieces()) area |= piece.pixels;
    return area.isEmpty() ? QRect(96, 96, 64, 64) : area.adjusted(-16, -16, 16, 16);
}

QImage AnimationsModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const QVector<int> cram = paletteList(session_->doc(Doc::Palettes), paletteList_);
    const QVector<sdk::CompositionPiece> list = pieces();
    // Earlier pieces are in front: draw the last first.
    for (int index = list.size() - 1; index >= 0; --index) {
        const QJsonObject piece = pieceObject(index);
        const QRect rect = list[index].pixels;
        const QString art = piece.value(QStringLiteral("art")).toString();
        const int attributes = piece.value(QStringLiteral("attributes")).toInt();
        if (art.isEmpty()) {
            painter.fillRect(rect, QColor(120, 140, 180, 70));
            painter.setPen(QColor(180, 200, 255, 200));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
            continue;
        }
        const QStringList tiles = artTiles(session_->doc(Doc::Tiles), art);
        const int w = rect.width() / 8, h = rect.height() / 8;
        const bool hflip = attributes & 0x800, vflip = attributes & 0x1000;
        const int line = (attributes >> 13) & 3 ? (attributes >> 13) & 3 : line_;
        // Sprite tiles run down each column before moving right.
        for (int column = 0; column < w; ++column) {
            for (int row = 0; row < h; ++row) {
                const QVector<quint8> pixels = tilePixels(hexBytes(tiles.value(column * h + row)));
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        const int value = pixels[(vflip ? 7 - py : py) * 8 + (hflip ? 7 - px : px)];
                        if (!value) continue;
                        const int dx = (hflip ? w - 1 - column : column) * 8 + px;
                        const int dy = (vflip ? h - 1 - row : row) * 8 + py;
                        if (rect.x() + dx < image.width() && rect.y() + dy < image.height() && rect.x() + dx >= 0 &&
                            rect.y() + dy >= 0) {
                            image.setPixel(rect.x() + dx, rect.y() + dy, cramColor(cram.value(line * 16 + value)));
                        }
                    }
                }
            }
        }
    }
    return image;
}

void AnimationsModel::selectPiece(const QString& id) {
    piece_ = id;
    emit selectionChanged();
}

void AnimationsModel::beginDrag(const QString& id) {
    selectPiece(id);
    dragBefore_ = session_->doc(Doc::Mappings);
    dragRevision_ = session_->revision(Doc::Mappings);
    const QJsonObject piece = pieceObject(id.toInt());
    dragStart_ = QPoint(piece.value(QStringLiteral("x")).toInt(), piece.value(QStringLiteral("y")).toInt());
}

bool AnimationsModel::dragPiece(const QString& id, QPoint offset) {
    const QPair<int, int> mapping = mappingOf(frame_);
    if (mapping.first < 0) return false;
    const int index = id.toInt();
    const int x = std::clamp(dragStart_.x() + offset.x(), -128, 127);
    const int y = std::clamp(dragStart_.y() + offset.y(), -128, 127);
    QJsonObject& document = session_->live(Doc::Mappings);
    QJsonArray blocks = document.value(QStringLiteral("blocks")).toArray();
    QJsonObject block = blocks.at(mapping.first).toObject();
    QJsonArray mappings = block.value(QStringLiteral("mappings")).toArray();
    QJsonObject item = mappings.at(mapping.second).toObject();
    QJsonArray list = item.value(QStringLiteral("pieces")).toArray();
    QJsonObject piece = list.at(index).toObject();
    piece.insert(QStringLiteral("x"), x);
    piece.insert(QStringLiteral("y"), y);
    list.replace(index, piece);
    item.insert(QStringLiteral("pieces"), list);
    mappings.replace(mapping.second, item);
    block.insert(QStringLiteral("mappings"), mappings);
    blocks.replace(mapping.first, block);
    document.insert(QStringLiteral("blocks"), blocks);
    session_->touch(Doc::Mappings);
    return true;
}

void AnimationsModel::endDrag() {
    session_->commit(Doc::Mappings, dragBefore_, dragRevision_, tr("Move sprite piece"));
}

QVector<sdk::ViewOption> AnimationsModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Colours");
    palette.value = paletteList_;
    for (const QJsonValue& value : session_->doc(Doc::Palettes).value(QStringLiteral("lists")).toArray()) {
        palette.choices << value.toObject().value(QStringLiteral("label")).toString();
    }
    sdk::ViewOption line;
    line.key = QStringLiteral("line");
    line.label = tr("Line");
    line.value = QString::number(line_);
    line.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    return {palette, line};
}

void AnimationsModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) paletteList_ = value;
    if (key == QLatin1String("line")) line_ = std::clamp(value.toInt(), 0, 3);
    emit toolsChanged();
    emit contentChanged();
}

QString AnimationsModel::statusText() const {
    return tr("%1 · entry %2 of %3").arg(script_).arg(frame_ + 1).arg(frames().size());
}

sdk::Inspection AnimationsModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject item = entry(frame_);
    if (!piece_.isEmpty()) {
        const QJsonObject piece = pieceObject(piece_.toInt());
        inspection.kicker = tr("Sprite piece");
        inspection.title = tr("Piece %1").arg(piece_.toInt() + 1);
        const auto field = [](const QString& key, const QString& label, int value, int minimum, int maximum) {
            sdk::Field result;
            result.key = key;
            result.label = label;
            result.type = sdk::Field::Type::Integer;
            result.value = value;
            result.minimum = minimum;
            result.maximum = maximum;
            result.monospace = true;
            return result;
        };
        inspection.fields = {field(QStringLiteral("x"), tr("X"), piece.value(QStringLiteral("x")).toInt(), -128, 127),
                             field(QStringLiteral("y"), tr("Y"), piece.value(QStringLiteral("y")).toInt(), -128, 127),
                             field(QStringLiteral("attributes"), tr("Attributes"),
                                   piece.value(QStringLiteral("attributes")).toInt(), 0, 0xFFFF)};
        if (!piece.contains(QStringLiteral("art"))) {
            inspection.fields.append(
                field(QStringLiteral("size"), tr("Size"), piece.value(QStringLiteral("size")).toInt(), 0, 0xFF));
        }
        inspection.note =
            piece.contains(QStringLiteral("art"))
                ? tr("The piece uploads its own art (%1); its tile and size follow that art.")
                      .arg(piece.value(QStringLiteral("art")).toString())
                : tr("The tile index is added to the object's own; bit 15 ends the mapping and stays as it is.");
        return inspection;
    }
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
    inspection.fields = {target, duration};
    inspection.note =
        tr("A frame lasts its duration in calls of the animation reader; an entry stays a frame or a jump.");
    return inspection;
}

bool AnimationsModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (!piece_.isEmpty() && (key == QLatin1String("x") || key == QLatin1String("y") ||
                              key == QLatin1String("attributes") || key == QLatin1String("size"))) {
        const QPair<int, int> mapping = mappingOf(frame_);
        const int index = piece_.toInt();
        const int number = value.toInt();
        const QJsonObject piece = pieceObject(index);
        if (key == QLatin1String("attributes") &&
            ((number & kLastPiece) != (piece.value(QStringLiteral("attributes")).toInt() & kLastPiece) ||
             (piece.contains(QStringLiteral("art")) &&
              (number & 0x7FF) != (piece.value(QStringLiteral("attributes")).toInt() & 0x7FF)))) {
            if (error) *error = tr("The end mark (and a dynamic piece's tile) stay as they are.");
            return false;
        }
        return session_->edit(
            Doc::Mappings, tr("Change sprite piece"),
            [&](QJsonObject& document) {
                QJsonArray blocks = document.value(QStringLiteral("blocks")).toArray();
                QJsonObject block = blocks.at(mapping.first).toObject();
                QJsonArray mappings = block.value(QStringLiteral("mappings")).toArray();
                QJsonObject item = mappings.at(mapping.second).toObject();
                QJsonArray list = item.value(QStringLiteral("pieces")).toArray();
                QJsonObject changed = list.at(index).toObject();
                changed.insert(key, number);
                list.replace(index, changed);
                item.insert(QStringLiteral("pieces"), list);
                mappings.replace(mapping.second, item);
                block.insert(QStringLiteral("mappings"), mappings);
                blocks.replace(mapping.first, block);
                document.insert(QStringLiteral("blocks"), blocks);
            },
            error);
    }
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
