#include "alien_soldier/plugin/metatiles_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QPainter>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

constexpr int kChunksPerRow = 8;

QString hex(int value) { return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(); }

}  // namespace

MetatilesModel::MetatilesModel(AlienSoldierSession* session, int stage)
    : TileMapModel(session), session_(session), stage_(stage) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Maps || doc == Doc::Tiles || doc == Doc::Compressed || doc == Doc::Palettes)
            emit contentChanged();
    });
}

int MetatilesModel::plane() const { return session_->stage(stage_).planes[0].present ? 0 : 1; }

QString MetatilesModel::title() const { return tr("%1 · chunks").arg(session_->stageLabel(stage_)); }

bool MetatilesModel::isReadOnly() const { return chunkCount(session_->stage(stage_), plane()) == 0; }

QString MetatilesModel::readOnlyReason() const { return tr("This stage slot loads no map."); }

QSize MetatilesModel::gridSize() const {
    const int chunks = std::max(1, chunkCount(session_->stage(stage_), plane()));
    return {kChunksPerRow * 8, ((chunks + kChunksPerRow - 1) / kChunksPerRow) * 8};
}

QVector<sdk::Tool> MetatilesModel::tools() const {
    if (isReadOnly()) return {};
    return {{QStringLiteral("paint"), tr("Paint patterns (right button picks)"), QStringLiteral("pencil")},
            {QStringLiteral("pick"), tr("Pick a pattern"), QStringLiteral("pick")}};
}

QImage MetatilesModel::render() const {
    const Scene& scene = session_->stage(stage_);
    const QSize size = gridSize() * 32;
    QImage image(size, QImage::Format_ARGB32);
    image.fill(QColor::fromRgb(cramColor(scene.cram.value(scene.backdrop))));
    QPainter painter(&image);
    const int chunks = chunkCount(scene, plane());
    for (int chunk = 0; chunk < chunks; ++chunk) {
        painter.drawImage((chunk % kChunksPerRow) * 256, (chunk / kChunksPerRow) * 256,
                          chunkImage(scene, plane(), chunk));
    }
    painter.setPen(QColor(255, 255, 255, 90));
    for (int x = 256; x < size.width(); x += 256) painter.drawLine(x, 0, x, size.height());
    for (int y = 256; y < size.height(); y += 256) painter.drawLine(0, y, size.width(), y);
    return image;
}

int MetatilesModel::slotOffset(QPoint cell) const {
    const Scene& scene = session_->stage(stage_);
    const int chunk = (cell.y() / 8) * kChunksPerRow + cell.x() / 8;
    if (cell.x() < 0 || cell.y() < 0 || chunk >= chunkCount(scene, plane())) return -1;
    return int(scene.planes[plane()].chunks - kRamBase) + chunk * 64 + (cell.y() % 8) * 8 + cell.x() % 8;
}

QVector<sdk::CellBrush> MetatilesModel::brushes() const {
    QVector<sdk::CellBrush> result;
    const int patterns = patternCount(session_->stage(stage_), plane());
    for (int pattern = 0; pattern < std::min(patterns, 256); ++pattern)
        result.append({pattern, tr("Pattern %1").arg(hex(pattern)), QColor()});
    return result;
}

void MetatilesModel::setCurrentBrush(int value) {
    brush_ = value;
    emit toolsChanged();
}

int MetatilesModel::cellValue(QPoint cell) const {
    const int offset = slotOffset(cell);
    const Scene& scene = session_->stage(stage_);
    return offset < 0 || offset >= scene.ram.size() ? 0 : quint8(scene.ram.at(offset));
}

bool MetatilesModel::isCellEditable(QPoint cell) const {
    const int offset = slotOffset(cell);
    return offset >= 0 &&
           mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->stage(stage_), offset).index >=
               0;
}

QString MetatilesModel::lockReason(QPoint cell) const {
    return slotOffset(cell) < 0 ? tr("There is no chunk here.") : tr("No map document holds this chunk.");
}

sdk::Library MetatilesModel::sheet() const {
    sdk::Library library;
    const Scene& scene = session_->stage(stage_);
    library.hint = tr("pick a pattern, then paint chunk slots");
    library.grid = true;
    library.columns = 8;
    library.cell = QSize(32, 32);
    const int patterns = std::min(patternCount(scene, plane()), 256);
    for (int pattern = 0; pattern < patterns; ++pattern) {
        sdk::LibraryItem item;
        item.id = QString::number(pattern);
        item.label = hex(pattern);
        item.icon = patternImage(scene, plane(), pattern);
        library.items.append(item);
    }
    return library;
}

void MetatilesModel::setCurrentLibraryItem(const QString& id) { setCurrentBrush(id.toInt()); }

QString MetatilesModel::currentLibraryItem() const { return QString::number(brush_); }

void MetatilesModel::beginStroke() {
    strokeBefore_ = session_->doc(Doc::Maps);
    strokeRevision_ = session_->revision(Doc::Maps);
}

bool MetatilesModel::paintCell(QPoint cell, int value) {
    const int offset = slotOffset(cell);
    if (offset < 0 || value < 0 || value > 0xFF) return false;
    if (cellValue(cell) == value) return true;
    const MapValue target =
        mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->stage(stage_), offset);
    if (target.index < 0 || target.doc != Doc::Maps) return false;
    try {
        setMapValue(session_->live(Doc::Maps), target, value);
    } catch (const std::exception&) {
        return false;
    }
    session_->touch(Doc::Maps);
    return true;
}

void MetatilesModel::endStroke() { session_->commit(Doc::Maps, strokeBefore_, strokeRevision_, tr("Paint patterns")); }

QString MetatilesModel::statusText() const {
    return tr("%1 chunks · %2 patterns")
        .arg(chunkCount(session_->stage(stage_), plane()))
        .arg(patternCount(session_->stage(stage_), plane()));
}

sdk::Inspection MetatilesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Chunks");
    inspection.title = session_->stageLabel(stage_);
    sdk::Field brush;
    brush.key = QStringLiteral("brush");
    brush.label = tr("Pattern");
    brush.value = hex(brush_);
    inspection.fields = {brush};
    inspection.note =
        tr("A chunk is 8x8 patterns and a pattern 4x4 tiles. Chunks other stages share change there too; "
           "the build compresses each map again into its slot.");
    return inspection;
}

}  // namespace rl::alien_soldier
