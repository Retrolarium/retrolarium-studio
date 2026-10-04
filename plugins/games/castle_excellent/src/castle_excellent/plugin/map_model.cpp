#include "castle/plugin/map_model.hpp"

#include "castle/plugin/castle_session.hpp"

#include <QPainter>

namespace rl::castle {

CastleMapModel::CastleMapModel(CastleSession* session) : TileMapModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &CastleSession::startChanged, this, [this] {
        emit contentChanged();
        emit selectionChanged();
        emit toolsChanged();
    });
    selected_ = session_->start().room;
}

QSize CastleMapModel::cellSize() const {
    return {CastlePainter::kCellWidth * 20 / divisor_, CastlePainter::kCellHeight * 20 / divisor_};
}

QVector<sdk::ViewOption> CastleMapModel::viewOptions() const {
    sdk::ViewOption scale;
    scale.key = QStringLiteral("scale");
    scale.label = QStringLiteral("Scale");
    scale.value = divisor_ == 1 ? QStringLiteral("1") : QStringLiteral("1/%1").arg(divisor_);
    scale.choices = QStringList{QStringLiteral("1/4"), QStringLiteral("1/2"), QStringLiteral("1")};
    return {scale};
}

void CastleMapModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("scale")) return;
    const int divisor = value == QLatin1String("1/4") ? 4 : value == QLatin1String("1/2") ? 2 : 1;
    if (divisor != divisor_) {
        divisor_ = divisor;
        emit contentChanged();
        emit toolsChanged();
    }
}

int CastleMapModel::roomIndex(int number) const {
    const CastleLevels& levels = session_->levels();
    for (int r = 0; r < levels.roomCount(); ++r) {
        if (levels.roomNumber(r) == number) return r;
    }
    return -1;
}

QVector<sdk::Tool> CastleMapModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select a room"), QStringLiteral("select")}};
}

QImage CastleMapModel::render() const {
    const QString startKey = session_->selectedStart();
    if (imageRevision_ == session_->revision() && imageDivisor_ == divisor_ && imageStart_ == startKey && !image_.isNull()) {
        return image_;
    }
    imageStart_ = startKey;
    const int kRoomWidth = cellSize().width();
    const int kRoomHeight = cellSize().height();
    QImage image(kColumns * kRoomWidth, kRows * kRoomHeight, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const CastleLevels& levels = session_->levels();
    const CastlePainter& art = session_->painter();
    for (int r = 0; r < levels.roomCount(); ++r) {
        const int number = levels.roomNumber(r);
        if (number < 1 || number > kColumns * kRows) continue;
        const QRect at(((number - 1) % kColumns) * kRoomWidth, ((number - 1) / kColumns) * kRoomHeight, kRoomWidth,
                       kRoomHeight);
        painter.drawImage(at, art.room(levels.cells(r), levels.objects(r)));
    }
    // The player where Play from selected starts.
    const CastleStart start = session_->start();
    if (start.room >= 1 && start.room <= kColumns * kRows) {
        const QPoint origin(((start.room - 1) % kColumns) * kRoomWidth, ((start.room - 1) / kColumns) * kRoomHeight);
        painter.drawImage(QRect(origin + start.pixel / divisor_, QSize(16, 16) / divisor_), art.player());
    }
    // Room borders, faint, so the grid reads at any zoom.
    painter.setPen(QColor(255, 255, 255, 40));
    for (int c = 1; c < kColumns; ++c) painter.drawLine(c * kRoomWidth, 0, c * kRoomWidth, image.height());
    for (int row = 1; row < kRows; ++row) painter.drawLine(0, row * kRoomHeight, image.width(), row * kRoomHeight);
    painter.end();
    image_ = image;
    imageRevision_ = session_->revision();
    imageDivisor_ = divisor_;
    return image_;
}

QVector<sdk::MapObject> CastleMapModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int number = 1; number <= kColumns * kRows; ++number) {
        sdk::MapObject o;
        o.id = QStringLiteral("room:%1").arg(number);
        o.label = roomLabel(number);
        o.cells = QRect((number - 1) % kColumns, (number - 1) / kColumns, 1, 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString CastleMapModel::selectedObject() const {
    return selected_ > 0 ? QStringLiteral("room:%1").arg(selected_) : QString();
}

void CastleMapModel::selectObject(const QString& id) {
    const int number = id.startsWith(QLatin1String("room:")) ? id.mid(5).toInt() : -1;
    if (number == selected_ || number < 1 || number > kColumns * kRows) return;
    selected_ = number;
    // The selected room is where Play from selected starts.
    CastleStart start = session_->start();
    start.room = number;
    session_->setStart(start);
    emit selectionChanged();
    emit toolsChanged();
}

QString CastleMapModel::startPoint() const {
    return session_->selectedStart();
}

QVector<sdk::EditorAction> CastleMapModel::actions() const {
    sdk::EditorAction open;
    open.id = QStringLiteral("open");
    open.label = selected_ > 0 ? QStringLiteral("Open %1").arg(roomLabel(selected_)) : QStringLiteral("Open room");
    open.icon = QStringLiteral("levels");
    open.tooltip = QStringLiteral("Edit the selected room's cells and objects");
    return {open};
}

void CastleMapModel::triggerAction(const QString& id) {
    const int index = roomIndex(selected_);
    if (id == QLatin1String("open") && index >= 0) {
        emit session_->documentRequested(QStringLiteral("rooms"), QString::number(index + 1));
    }
}

sdk::Inspection CastleMapModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle.prefix-rooms@1");
    const int index = roomIndex(selected_);
    if (index < 0) {
        inspection.kicker = QStringLiteral("Castle");
        inspection.title = title();
        inspection.note = QStringLiteral("All 100 rooms where the game puts them: ten to a row, the room above is ten "
                                         "less, the room below ten more. Select a room to start Play from selected "
                                         "there; Open room edits it.");
        return inspection;
    }
    const CastleLevels& levels = session_->levels();
    inspection.kicker = QStringLiteral("Castle map");
    inspection.title = roomLabel(selected_);
    const auto neighbour = [this](int number) {
        return number >= 1 && number <= kColumns * kRows && roomIndex(number) >= 0 ? roomLabel(number) : QStringLiteral("—");
    };
    sdk::Field place;
    place.key = QStringLiteral("place");
    place.label = QStringLiteral("Grid");
    place.value = QStringLiteral("column %1, row %2").arg((selected_ - 1) % kColumns + 1).arg((selected_ - 1) / kColumns + 1);
    sdk::Field objects;
    objects.key = QStringLiteral("objects");
    objects.label = QStringLiteral("Objects");
    objects.value = QString::number(levels.objects(index).size());
    sdk::Field above;
    above.key = QStringLiteral("above");
    above.label = QStringLiteral("Above / below");
    above.value = QStringLiteral("%1 / %2").arg(neighbour(selected_ - kColumns), neighbour(selected_ + kColumns));
    sdk::Field sides;
    sides.key = QStringLiteral("sides");
    sides.label = QStringLiteral("Left / right");
    sides.value = QStringLiteral("%1 / %2")
                      .arg((selected_ - 1) % kColumns > 0 ? neighbour(selected_ - 1) : QStringLiteral("—"),
                           (selected_ - 1) % kColumns < kColumns - 1 ? neighbour(selected_ + 1) : QStringLiteral("—"));
    inspection.fields = {place, objects, above, sides};
    inspection.note = QStringLiteral("Play from selected starts in this room at the player's start point. Open room "
                                     "edits its cells and objects. Horizontal exits follow the grid except where the "
                                     "game's edge tables mark a castle wall.");
    return inspection;
}

QString CastleMapModel::statusText() const {
    return selected_ > 0 ? QStringLiteral("Castle map · %1").arg(roomLabel(selected_)) : QStringLiteral("Castle map");
}

}  // namespace rl::castle
