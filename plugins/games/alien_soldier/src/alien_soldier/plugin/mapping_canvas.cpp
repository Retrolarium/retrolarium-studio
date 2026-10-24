#include "alien_soldier/plugin/mapping_canvas.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

constexpr int kLastPiece = 0x8000;

}  // namespace

MappingCanvas::MappingCanvas(AlienSoldierSession* session) : CompositionModel(session), session_(session) {
    paletteList_ = session_->doc(Doc::Stages)
                       .value(QStringLiteral("scenes"))
                       .toArray()
                       .at(0)
                       .toObject()
                       .value(QStringLiteral("palette_list"))
                       .toString();
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Animations || doc == Doc::Mappings || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes)
            emit contentChanged();
    });
}

QString MappingCanvas::blockLabel() const {
    const QPair<int, int> shown = mapping();
    if (shown.first < 0) return {};
    return session_->doc(Doc::Mappings)
        .value(QStringLiteral("blocks"))
        .toArray()
        .at(shown.first)
        .toObject()
        .value(QStringLiteral("label"))
        .toString();
}

QJsonArray MappingCanvas::mappingPieces() const {
    const QPair<int, int> shown = mapping();
    if (shown.first < 0) return {};
    return session_->doc(Doc::Mappings)
        .value(QStringLiteral("blocks"))
        .toArray()
        .at(shown.first)
        .toObject()
        .value(QStringLiteral("mappings"))
        .toArray()
        .at(shown.second)
        .toObject()
        .value(QStringLiteral("pieces"))
        .toArray();
}

QJsonObject MappingCanvas::pieceObject(int index) const { return mappingPieces().at(index).toObject(); }

SpriteContext MappingCanvas::context() const { return spriteContext(session_->docs(), blockLabel()); }

QVector<sdk::CompositionPiece> MappingCanvas::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const int attribute = context().attribute;
    const QJsonArray list = mappingPieces();
    for (int index = 0; index < list.size(); ++index) {
        const QJsonObject piece = list.at(index).toObject();
        sdk::CompositionPiece item;
        item.id = QString::number(index);
        item.label = tr("Piece %1").arg(index + 1);
        item.pixels = pieceRect(piece, attribute).translated(origin());
        result.append(item);
        if (piece.value(QStringLiteral("attributes")).toInt() & kLastPiece) break;
    }
    return result;
}

QRect MappingCanvas::focusRect() const {
    QRect area;
    for (const sdk::CompositionPiece& piece : pieces()) area |= piece.pixels;
    return area.isEmpty() ? QRect(96, 96, 64, 64) : area.adjusted(-16, -16, 16, 16);
}

QImage MappingCanvas::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    drawMapping(image, origin(), mappingPieces(), session_->docs(), context(),
                paletteList(session_->doc(Doc::Palettes), paletteList_), line_);
    return image;
}

void MappingCanvas::selectPiece(const QString& id) {
    piece_ = id;
    emit selectionChanged();
}

void MappingCanvas::beginDrag(const QString& id) {
    selectPiece(id);
    dragBefore_ = session_->doc(Doc::Mappings);
    dragRevision_ = session_->revision(Doc::Mappings);
    const QJsonObject piece = pieceObject(id.toInt());
    dragStart_ = QPoint(piece.value(QStringLiteral("x")).toInt(), piece.value(QStringLiteral("y")).toInt());
}

bool MappingCanvas::dragPiece(const QString& id, QPoint offset) {
    const QPair<int, int> shown = mapping();
    if (shown.first < 0) return false;
    // The object's flips mirror the pieces on the canvas.
    const int attribute = context().attribute;
    if (attribute & 0x800) offset.setX(-offset.x());
    if (attribute & 0x1000) offset.setY(-offset.y());
    const int index = id.toInt();
    const int x = std::clamp(dragStart_.x() + offset.x(), -128, 127);
    const int y = std::clamp(dragStart_.y() + offset.y(), -128, 127);
    QJsonObject& document = session_->live(Doc::Mappings);
    QJsonArray blocks = document.value(QStringLiteral("blocks")).toArray();
    QJsonObject block = blocks.at(shown.first).toObject();
    QJsonArray mappings = block.value(QStringLiteral("mappings")).toArray();
    QJsonObject item = mappings.at(shown.second).toObject();
    QJsonArray list = item.value(QStringLiteral("pieces")).toArray();
    QJsonObject piece = list.at(index).toObject();
    piece.insert(QStringLiteral("x"), x);
    piece.insert(QStringLiteral("y"), y);
    list.replace(index, piece);
    item.insert(QStringLiteral("pieces"), list);
    mappings.replace(shown.second, item);
    block.insert(QStringLiteral("mappings"), mappings);
    blocks.replace(shown.first, block);
    document.insert(QStringLiteral("blocks"), blocks);
    session_->touch(Doc::Mappings);
    return true;
}

void MappingCanvas::endDrag() { session_->commit(Doc::Mappings, dragBefore_, dragRevision_, tr("Move sprite piece")); }

QVector<sdk::ViewOption> MappingCanvas::viewOptions() const {
    // The colours of pieces the game repository records nothing for.
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

void MappingCanvas::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) paletteList_ = value;
    if (key == QLatin1String("line")) line_ = std::clamp(value.toInt(), 0, 3);
    emit toolsChanged();
    emit contentChanged();
}

void MappingCanvas::inspectPiece(sdk::Inspection& inspection) const {
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
}

bool MappingCanvas::setPieceField(const QString& key, const QVariant& value, QString* error) {
    if (piece_.isEmpty() || (key != QLatin1String("x") && key != QLatin1String("y") &&
                             key != QLatin1String("attributes") && key != QLatin1String("size"))) {
        return false;
    }
    const QPair<int, int> shown = mapping();
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
            QJsonObject block = blocks.at(shown.first).toObject();
            QJsonArray mappings = block.value(QStringLiteral("mappings")).toArray();
            QJsonObject item = mappings.at(shown.second).toObject();
            QJsonArray list = item.value(QStringLiteral("pieces")).toArray();
            QJsonObject changed = list.at(index).toObject();
            changed.insert(key, number);
            list.replace(index, changed);
            item.insert(QStringLiteral("pieces"), list);
            mappings.replace(shown.second, item);
            block.insert(QStringLiteral("mappings"), mappings);
            blocks.replace(shown.first, block);
            document.insert(QStringLiteral("blocks"), blocks);
        },
        error);
}

}  // namespace rl::alien_soldier
