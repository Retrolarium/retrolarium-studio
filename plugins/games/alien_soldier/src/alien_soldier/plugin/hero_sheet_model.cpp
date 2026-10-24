#include "alien_soldier/plugin/hero_sheet_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/data/sprites.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

int heightOf(int size) { return (size & 3) + 1; }

// The tiles of an uncompressed art payload, all blocks in order.
QStringList artTiles(const QJsonObject& tiles, const QString& label) {
    const QJsonArray payloads = tiles.value(QStringLiteral("payloads")).toArray();
    const QJsonObject payload = payloads.at(indexOfLabel(payloads, label)).toObject();
    QStringList result;
    for (const QJsonValue& block : payload.value(QStringLiteral("blocks")).toArray()) {
        for (const QJsonValue& tile : block.toObject().value(QStringLiteral("tiles")).toArray())
            result << tile.toString();
    }
    return result;
}

// Writes tile `index` of an uncompressed art payload.
void setArtTile(QJsonObject& tiles, const QString& label, int index, const QString& hex) {
    QJsonArray payloads = tiles.value(QStringLiteral("payloads")).toArray();
    const int at = indexOfLabel(payloads, label);
    QJsonObject payload = payloads.at(at).toObject();
    QJsonArray blocks = payload.value(QStringLiteral("blocks")).toArray();
    for (int block = 0; block < blocks.size(); ++block) {
        QJsonObject item = blocks.at(block).toObject();
        QJsonArray list = item.value(QStringLiteral("tiles")).toArray();
        if (index < list.size()) {
            list.replace(index, hex);
            item.insert(QStringLiteral("tiles"), list);
            blocks.replace(block, item);
            break;
        }
        index -= list.size();
    }
    payload.insert(QStringLiteral("blocks"), blocks);
    payloads.replace(at, payload);
    tiles.insert(QStringLiteral("payloads"), payloads);
}

QString pixelsHex(const QVector<quint8>& pixels) {
    QString text;
    for (quint8 pixel : pixels) text += QString::number(pixel, 16).toUpper();
    return text;
}

}  // namespace

HeroSheetModel::HeroSheetModel(AlienSoldierSession* session) : TileModel(session), session_(session) {
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    for (int block = 0; block < blocks.size(); ++block) {
        const QJsonObject item = blocks.at(block).toObject();
        if (item.value(QStringLiteral("kind")).toString() != QLatin1String("dynamic")) continue;
        const QJsonArray mappings = item.value(QStringLiteral("mappings")).toArray();
        for (int mapping = 0; mapping < mappings.size(); ++mapping) frames_.append({block, mapping});
    }
    for (int frame = 0; frame < frames_.size(); ++frame) {
        for (const QJsonValue& piece : pieces(frame)) bounds_ |= pieceRect(piece.toObject(), 0);
    }
    if (bounds_.isEmpty()) bounds_ = QRect(0, 0, 8, 8);
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc changed) {
        if (changed == Doc::Tiles || changed == Doc::Mappings || changed == Doc::Palettes) emit contentChanged();
    });
}

QJsonArray HeroSheetModel::pieces(int frame) const {
    if (frame < 0 || frame >= frames_.size()) return {};
    return session_->doc(Doc::Mappings)
        .value(QStringLiteral("blocks"))
        .toArray()
        .at(frames_[frame].first)
        .toObject()
        .value(QStringLiteral("mappings"))
        .toArray()
        .at(frames_[frame].second)
        .toObject()
        .value(QStringLiteral("pieces"))
        .toArray();
}

QString HeroSheetModel::blockLabel(int frame) const {
    if (frame < 0 || frame >= frames_.size()) return {};
    return session_->doc(Doc::Mappings)
        .value(QStringLiteral("blocks"))
        .toArray()
        .at(frames_[frame].first)
        .toObject()
        .value(QStringLiteral("label"))
        .toString();
}

QString HeroSheetModel::title() const { return tr("Hero sprite sheet"); }

QVector<QColor> HeroSheetModel::penColors() const {
    // The colours the game repository records for the player's mappings,
    // else the first stage's.
    const SpriteContext context = spriteContext(session_->docs(), blockLabel(frame_));
    int line = 2;
    QVector<int> cram = context.cram;
    if (!context.known) {
        cram = paletteList(session_->doc(Doc::Palettes), session_->doc(Doc::Stages)
                                                             .value(QStringLiteral("scenes"))
                                                             .toArray()
                                                             .at(0)
                                                             .toObject()
                                                             .value(QStringLiteral("palette_list"))
                                                             .toString());
    } else {
        line = (context.attribute >> 13) & 3;
    }
    QVector<QColor> colors;
    for (int pen = 0; pen < 16; ++pen) colors.append(QColor::fromRgb(cramColor(cram.value(line * 16 + pen))));
    return colors;
}

HeroSheetModel::Hit HeroSheetModel::hitAt(int frame, QPoint pixel) const {
    const QJsonArray list = pieces(frame);
    const QPoint at = pixel + bounds_.topLeft();
    for (const QJsonValue& value : list) {
        const QJsonObject piece = value.toObject();
        const QRect rect = pieceRect(piece, 0);
        if (!rect.contains(at)) continue;
        const int attributes = piece.value(QStringLiteral("attributes")).toInt();
        const int h = heightOf(piece.value(QStringLiteral("size")).toInt());
        const int w = rect.width() / 8;
        int x = at.x() - rect.x(), y = at.y() - rect.y();
        if (attributes & 0x800) x = rect.width() - 1 - x;
        if (attributes & 0x1000) y = rect.height() - 1 - y;
        Q_UNUSED(w);
        // Sprite tiles run down each column before moving right.
        Hit hit;
        hit.art = piece.value(QStringLiteral("art")).toString();
        hit.tile = (x / 8) * h + y / 8;
        hit.pixel = QPoint(x % 8, y % 8);
        return hit;
    }
    return {};
}

QVector<quint8> HeroSheetModel::tilePixels(int tile) const {
    const QSize size = bounds_.size();
    QVector<quint8> pixels(size.width() * size.height(), 0);
    const QJsonObject tiles = session_->doc(Doc::Tiles);
    const QJsonArray list = pieces(tile);
    // Earlier pieces are in front: the last is drawn first.
    for (int index = list.size() - 1; index >= 0; --index) {
        const QJsonObject piece = list.at(index).toObject();
        const QRect rect = pieceRect(piece, 0).translated(-bounds_.topLeft());
        const int attributes = piece.value(QStringLiteral("attributes")).toInt();
        const bool hflip = attributes & 0x800, vflip = attributes & 0x1000;
        const int w = rect.width() / 8, h = rect.height() / 8;
        const QStringList art = artTiles(tiles, piece.value(QStringLiteral("art")).toString());
        for (int column = 0; column < w; ++column) {
            for (int row = 0; row < h; ++row) {
                // Sprite tiles run down each column before moving right.
                const QVector<quint8> source = rl::alien_soldier::tilePixels(hexBytes(art.value(column * h + row)));
                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        const int value = source.value((vflip ? 7 - py : py) * 8 + (hflip ? 7 - px : px));
                        if (!value) continue;
                        const int x = rect.x() + (hflip ? w - 1 - column : column) * 8 + px;
                        const int y = rect.y() + (vflip ? h - 1 - row : row) * 8 + py;
                        if (x >= 0 && y >= 0 && x < size.width() && y < size.height())
                            pixels[y * size.width() + x] = quint8(value);
                    }
                }
            }
        }
    }
    return pixels;
}

bool HeroSheetModel::isTileDirty(int tile) const {
    const QJsonObject now = session_->doc(Doc::Tiles), saved = session_->savedDoc(Doc::Tiles);
    for (const QJsonValue& piece : pieces(tile)) {
        const QString art = piece.toObject().value(QStringLiteral("art")).toString();
        if (artTiles(now, art) != artTiles(saved, art)) return true;
    }
    return false;
}

void HeroSheetModel::setCurrentTile(int tile) {
    frame_ = std::clamp(tile, 0, std::max(0, int(frames_.size()) - 1));
    emit selectionChanged();
}

void HeroSheetModel::setCurrentPen(int pen) {
    pen_ = std::clamp(pen, 0, 15);
    emit selectionChanged();
}

QVector<sdk::Tool> HeroSheetModel::tools() const {
    return {{QStringLiteral("pencil"), tr("Pencil"), QStringLiteral("pencil")},
            {QStringLiteral("pick"), tr("Pick a colour"), QStringLiteral("pick")}};
}

void HeroSheetModel::beginStroke() {
    strokeBefore_ = session_->doc(Doc::Tiles);
    strokeRevision_ = session_->revision(Doc::Tiles);
}

bool HeroSheetModel::setPixel(QPoint pixel, int pen) {
    if (pen < 0 || pen > 15) return false;
    const Hit hit = hitAt(frame_, pixel);
    if (hit.art.isEmpty()) return false;
    QJsonObject& tiles = session_->live(Doc::Tiles);
    const QStringList art = artTiles(tiles, hit.art);
    if (hit.tile < 0 || hit.tile >= art.size()) return false;
    QVector<quint8> pixels = rl::alien_soldier::tilePixels(hexBytes(art.at(hit.tile)));
    quint8& target = pixels[hit.pixel.y() * 8 + hit.pixel.x()];
    if (target == pen) return true;
    target = quint8(pen);
    setArtTile(tiles, hit.art, hit.tile, pixelsHex(pixels));
    session_->touch(Doc::Tiles);
    return true;
}

void HeroSheetModel::endStroke() { session_->commit(Doc::Tiles, strokeBefore_, strokeRevision_, tr("Paint the hero")); }

QString HeroSheetModel::statusText() const {
    return tr("%1 · frame %2 of %3").arg(blockLabel(frame_)).arg(frame_ + 1).arg(frames_.size());
}

sdk::Inspection HeroSheetModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Hero frame");
    inspection.title = blockLabel(frame_);
    QStringList art;
    for (const QJsonValue& piece : pieces(frame_)) art << piece.toObject().value(QStringLiteral("art")).toString();
    sdk::Field field;
    field.key = QStringLiteral("art");
    field.label = tr("Art");
    field.type = sdk::Field::Type::ReadOnly;
    field.value = art.join(QStringLiteral(", "));
    field.monospace = true;
    inspection.fields = {field};
    inspection.note =
        tr("Each frame is put together from the art pieces its mapping uploads; painting changes the "
           "piece under the pen, in every frame that shows it.");
    return inspection;
}

}  // namespace rl::alien_soldier
