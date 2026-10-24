#include "alien_soldier/plugin/tiles_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

QString pixelsHex(const QVector<quint8>& pixels) {
    QString text;
    for (quint8 pixel : pixels) text += QString::number(pixel, 16).toUpper();
    return text;
}

}  // namespace

TilesModel::TilesModel(AlienSoldierSession* session, Doc doc, const QString& label)
    : TileModel(session), session_(session), doc_(doc), label_(label) {
    const QJsonArray lists = session_->doc(Doc::Palettes).value(QStringLiteral("lists")).toArray();
    const QJsonArray art = session_->doc(Doc::Art).value(QStringLiteral("payloads")).toArray();
    const QJsonObject entry = art.at(indexOfLabel(art, label_)).toObject();
    colours_ = entry.value(QStringLiteral("colours")).toObject();
    line_ = entry.value(QStringLiteral("line")).toInt();
    if (colours_.isEmpty()) {
        // The first stage's colours to start with; any list can be picked.
        const QString first = session_->doc(Doc::Stages)
                                  .value(QStringLiteral("scenes"))
                                  .toArray()
                                  .at(0)
                                  .toObject()
                                  .value(QStringLiteral("palette_list"))
                                  .toString();
        paletteList_ =
            indexOfLabel(lists, first) >= 0 ? first : lists.at(0).toObject().value(QStringLiteral("label")).toString();
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc changed) {
        if (changed == doc_ || changed == Doc::Palettes) emit contentChanged();
    });
}

QStringList TilesModel::tiles(const QJsonObject& document) const {
    QStringList result;
    const QJsonArray payloads = document.value(QStringLiteral("payloads")).toArray();
    const QJsonObject payload = payloads.at(indexOfLabel(payloads, label_)).toObject();
    if (doc_ == Doc::Compressed) {
        for (const QJsonValue& tile : payload.value(QStringLiteral("tiles")).toArray()) result << tile.toString();
    } else {
        for (const QJsonValue& block : payload.value(QStringLiteral("blocks")).toArray()) {
            for (const QJsonValue& tile : block.toObject().value(QStringLiteral("tiles")).toArray())
                result << tile.toString();
        }
    }
    return result;
}

void TilesModel::setTile(QJsonObject& document, int index, const QString& hex) const {
    QJsonArray payloads = document.value(QStringLiteral("payloads")).toArray();
    const int at = indexOfLabel(payloads, label_);
    QJsonObject payload = payloads.at(at).toObject();
    if (doc_ == Doc::Compressed) {
        QJsonArray list = payload.value(QStringLiteral("tiles")).toArray();
        list.replace(index, hex);
        payload.insert(QStringLiteral("tiles"), list);
    } else {
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
    }
    payloads.replace(at, payload);
    document.insert(QStringLiteral("payloads"), payloads);
}

int TilesModel::tileCount() const { return tiles(session_->doc(doc_)).size(); }

QVector<QColor> TilesModel::penColors() const {
    const QJsonObject palettes = session_->doc(Doc::Palettes);
    QVector<int> cram;
    if (!paletteList_.isEmpty()) {
        cram = paletteList(palettes, paletteList_);
    } else if (colours_.contains(QStringLiteral("list"))) {
        cram = paletteList(palettes, colours_.value(QStringLiteral("list")).toString());
        if (colours_.contains(QStringLiteral("command")))
            playPaletteCommand(cram, palettes, colours_.value(QStringLiteral("command")).toString());
    } else {
        cram = QVector<int>(64, 0);
        for (const QJsonValue& value : colours_.value(QStringLiteral("commands")).toArray()) {
            const QJsonArray pair = value.toArray();
            playPaletteCommand(cram, palettes, pair.at(0).toString(), pair.at(1).toInt());
        }
    }
    QVector<QColor> colors;
    for (int pen = 0; pen < 16; ++pen) colors.append(QColor::fromRgb(cramColor(cram.value(line_ * 16 + pen))));
    return colors;
}

QVector<quint8> TilesModel::tilePixels(int tile) const {
    return rl::alien_soldier::tilePixels(hexBytes(tiles(session_->doc(doc_)).value(tile)));
}

bool TilesModel::isTileDirty(int tile) const {
    return tiles(session_->doc(doc_)).value(tile) != tiles(session_->savedDoc(doc_)).value(tile);
}

void TilesModel::setCurrentTile(int tile) {
    tile_ = std::clamp(tile, 0, std::max(0, tileCount() - 1));
    emit selectionChanged();
}

void TilesModel::setCurrentPen(int pen) {
    pen_ = std::clamp(pen, 0, 15);
    emit selectionChanged();
}

void TilesModel::beginStroke() {
    strokeBefore_ = session_->doc(doc_);
    strokeRevision_ = session_->revision(doc_);
}

bool TilesModel::setPixel(QPoint pixel, int pen) {
    if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() > 7 || pixel.y() > 7 || pen < 0 || pen > 15) return false;
    QVector<quint8> pixels = tilePixels(tile_);
    quint8& target = pixels[pixel.y() * 8 + pixel.x()];
    if (target == pen) return true;
    target = quint8(pen);
    setTile(session_->live(doc_), tile_, pixelsHex(pixels));
    session_->touch(doc_);
    return true;
}

void TilesModel::endStroke() { session_->commit(doc_, strokeBefore_, strokeRevision_, tr("Paint tile")); }

bool TilesModel::fill(QPoint pixel, int pen, QString* error) {
    QVector<quint8> pixels = tilePixels(tile_);
    const quint8 from = pixels.value(pixel.y() * 8 + pixel.x());
    if (from == pen) return true;
    QVector<QPoint> pending{pixel};
    while (!pending.isEmpty()) {
        const QPoint at = pending.takeLast();
        if (at.x() < 0 || at.y() < 0 || at.x() > 7 || at.y() > 7 || pixels[at.y() * 8 + at.x()] != from) continue;
        pixels[at.y() * 8 + at.x()] = quint8(pen);
        pending << at + QPoint(1, 0) << at - QPoint(1, 0) << at + QPoint(0, 1) << at - QPoint(0, 1);
    }
    const QString hex = pixelsHex(pixels);
    return session_->edit(doc_, tr("Fill tile"), [&](QJsonObject& document) { setTile(document, tile_, hex); }, error);
}

sdk::Budget TilesModel::budget() const {
    sdk::Budget budget;
    if (doc_ != Doc::Compressed) return budget;
    const QJsonArray payloads = session_->doc(doc_).value(QStringLiteral("payloads")).toArray();
    const QJsonObject payload = payloads.at(indexOfLabel(payloads, label_)).toObject();
    budget.text = tr("%1 · slot of %2 bytes")
                      .arg(payload.value(QStringLiteral("codec")).toString())
                      .arg(payload.value(QStringLiteral("capacity")).toInt());
    return budget;
}

QVector<sdk::ViewOption> TilesModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Colours");
    palette.value = paletteList_.isEmpty() ? tr("As in the game") : paletteList_;
    if (!colours_.isEmpty()) palette.choices << tr("As in the game");
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

void TilesModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) paletteList_ = value == tr("As in the game") ? QString() : value;
    if (key == QLatin1String("line")) line_ = std::clamp(value.toInt(), 0, 3);
    emit toolsChanged();
    emit contentChanged();
}

QString TilesModel::statusText() const { return tr("%1 · tile %2 of %3").arg(label_).arg(tile_).arg(tileCount()); }

sdk::Inspection TilesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = doc_ == Doc::Compressed ? tr("Compressed art") : tr("Art");
    inspection.title = label_;
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Tile");
    tile.value = tr("%1 of %2").arg(tile_).arg(tileCount());
    sdk::Field pen;
    pen.key = QStringLiteral("pen");
    pen.label = tr("Pen");
    pen.value = pen_;
    inspection.fields = {tile, pen};
    inspection.note = doc_ == Doc::Compressed
                          ? tr("The build compresses the edited tiles again; they must fit the payload's slot, or the "
                               "build says so.")
                          : tr("The payload keeps its size and its blocks.");
    return inspection;
}

}  // namespace rl::alien_soldier
