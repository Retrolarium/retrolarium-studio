#include "doraemon/plugin/route_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace {

constexpr int kPanelWidth = 16;
constexpr int kPanelHeight = 15;
const QString kScreens = QStringLiteral("world2-screens");

const char* const kDirections[4] = {"right", "up", "down", "left"};

int screenValue(const QJsonArray& screens, int selector, int raw) {
    return screens.at(selector).toArray().at(raw / 16).toArray().at(raw % 16).toInt(DoraemonRouteModel::kPadding);
}

}  // namespace

DoraemonRouteModel::DoraemonRouteModel(DoraemonSession* session, int route)
    : TileMapModel(session), session_(session), route_(route) {
    readPanels();
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::selectedStartChanged, this, &sdk::EditorModel::toolsChanged);
    // Play from selected starts World 2 at this route's part (2A and 2B
    // branch off Part 2).
    const QString part = name_.section(QLatin1Char(' '), 1, 1);
    session_->setStartStage(part.startsWith(QLatin1Char('3')) ? 2 : part.startsWith(QLatin1Char('2')) ? 1 : 0);
}

QString DoraemonRouteModel::startPoint() const {
    return session_->selectedStart();
}

void DoraemonRouteModel::readPanels() {
    const QJsonObject r = session_->world2View().value(QStringLiteral("routes")).toArray().at(route_).toObject();
    name_ = r.value(QStringLiteral("name")).toString();
    columns_ = std::max(1, r.value(QStringLiteral("columns")).toInt());
    rows_ = std::max(1, r.value(QStringLiteral("rows")).toInt());
    panelAt_ = QVector<int>(columns_ * rows_, -1);
    panels_.clear();
    for (const QJsonValue& v : r.value(QStringLiteral("panels")).toArray()) {
        const QJsonObject p = v.toObject();
        Panel panel;
        panel.selector = p.value(QStringLiteral("selector")).toInt();
        panel.direction = p.value(QStringLiteral("direction")).toInt();
        panel.palette = p.value(QStringLiteral("palette")).toInt();
        panel.x = p.value(QStringLiteral("x")).toInt();
        panel.y = p.value(QStringLiteral("y")).toInt();
        const QJsonArray rows = p.value(QStringLiteral("cells")).toArray();
        const QJsonArray fixed = p.value(QStringLiteral("fixed")).toArray();
        for (int y = 0; y < kPanelHeight; ++y) {
            for (int x = 0; x < kPanelWidth; ++x) {
                const QJsonArray c = rows.at(y).toArray().at(x).toArray();
                panel.raw << (c.size() == 2 ? c.at(1).toInt() * 16 + c.at(0).toInt() : -1);
                panel.fixed << fixed.at(y).toArray().at(x).toInt(kPadding);
            }
        }
        if (panel.x >= 0 && panel.y >= 0 && panel.x < columns_ && panel.y < rows_) {
            panelAt_[panel.y * columns_ + panel.x] = panels_.size();
        }
        panels_ << panel;
    }
    annotations_.clear();
    for (const QJsonValue& v : r.value(QStringLiteral("annotations")).toArray()) annotations_ << v.toObject();
}

QString DoraemonRouteModel::title() const {
    return name_;
}

QSize DoraemonRouteModel::gridSize() const {
    return {columns_ * kPanelWidth, rows_ * kPanelHeight};
}

DoraemonRouteModel::Located DoraemonRouteModel::locate(QPoint cell) const {
    Located result;
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= columns_ * kPanelWidth || cell.y() >= rows_ * kPanelHeight) return result;
    const int index = panelAt_.value((cell.y() / kPanelHeight) * columns_ + cell.x() / kPanelWidth, -1);
    if (index < 0) return result;
    const Panel& p = panels_.at(index);
    const int local = (cell.y() % kPanelHeight) * kPanelWidth + cell.x() % kPanelWidth;
    result.panel = index;
    result.selector = p.selector;
    result.raw = p.raw.at(local);
    result.fixed = p.fixed.at(local);
    return result;
}

int DoraemonRouteModel::valueAt(const Located& at) const {
    if (at.panel < 0) return -1;
    if (at.raw < 0) return at.fixed;
    const auto painted = stroke_.constFind({at.selector, at.raw});
    if (painted != stroke_.constEnd()) return *painted;
    return screenValue(session_->resource(kScreens).value(QStringLiteral("screens")).toArray(), at.selector, at.raw);
}

QVector<sdk::Tool> DoraemonRouteModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select a cell"), QStringLiteral("select")},
            {QStringLiteral("paint"), QStringLiteral("Paint the sheet's metatile (right button takes one)"), QStringLiteral("paint")},
            {QStringLiteral("pick"), QStringLiteral("Take a cell's metatile as the brush"), QStringLiteral("pick")}};
}

QImage DoraemonRouteModel::render() const {
    const QSize grid = gridSize();
    QImage image(grid.width() * 16, grid.height() * 16, QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    QPainter painter(&image);
    const QJsonArray screens = session_->resource(kScreens).value(QStringLiteral("screens")).toArray();
    const QJsonArray saved = session_->savedBundle().resource(kScreens).value(QStringLiteral("screens")).toArray();
    const DoraemonPainter& art = session_->painter();
    QFont font = painter.font();
    font.setPixelSize(9);
    font.setBold(true);
    painter.setFont(font);
    for (const Panel& p : panels_) {
        const QPoint origin(p.x * kPanelWidth * 16, p.y * kPanelHeight * 16);
        for (int local = 0; local < kPanelWidth * kPanelHeight; ++local) {
            const int raw = p.raw.at(local);
            int value = p.fixed.at(local);
            bool changed = false;
            if (raw >= 0) {
                const auto painted = stroke_.constFind({p.selector, raw});
                value = painted != stroke_.constEnd() ? *painted : screenValue(screens, p.selector, raw);
                changed = value != screenValue(saved, p.selector, raw);
            }
            const QPoint at = origin + QPoint((local % kPanelWidth) * 16, (local / kPanelWidth) * 16);
            if (value < kSpawnBase) {
                painter.drawImage(at, art.metatile(value, p.palette));
            } else if (value < kPadding) {
                // An enemy spawn: the empty metatile with its state.
                painter.drawImage(at, art.metatile(0, p.palette));
                painter.fillRect(QRect(at, QSize(16, 16)), QColor(230, 60, 60, 150));
                painter.setPen(Qt::white);
                painter.drawText(QRect(at, QSize(16, 16)), Qt::AlignCenter, QString::number(value - kSpawnBase, 16).toUpper());
            } else {
                painter.fillRect(QRect(at, QSize(16, 16)), Qt::black);
            }
            if (changed) {
                painter.setPen(QColor(255, 200, 60, 200));
                painter.drawRect(QRect(at, QSize(15, 15)));
            }
        }
    }
    // Panel joins and the branch annotations.
    painter.setPen(QColor(255, 255, 255, 60));
    for (const Panel& p : panels_) painter.drawRect(p.x * kPanelWidth * 16, p.y * kPanelHeight * 16, kPanelWidth * 16 - 1, kPanelHeight * 16 - 1);
    font.setPixelSize(14);
    painter.setFont(font);
    for (const QJsonObject& a : annotations_) {
        const QRect box(a.value(QStringLiteral("x")).toInt() * kPanelWidth * 16 + 8, a.value(QStringLiteral("y")).toInt() * kPanelHeight * 16 + 8,
                        200, 22);
        painter.fillRect(box, QColor(0, 0, 0, 170));
        painter.setPen(QColor(255, 220, 120));
        painter.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, a.value(QStringLiteral("text")).toString());
    }
    return image;
}

QVector<sdk::CellBrush> DoraemonRouteModel::brushes() const {
    return {{metatile_, QStringLiteral("Metatile %1").arg(nes::hex(metatile_)), QColor()}};
}

void DoraemonRouteModel::setCurrentBrush(int value) {
    if (value < 0 || value >= kSpawnBase) return;
    metatile_ = value;
    emit toolsChanged();
    emit selectionChanged();
}

QString DoraemonRouteModel::currentLibraryItem() const {
    return QStringLiteral("metatile:%1").arg(metatile_);
}

void DoraemonRouteModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("metatile:"))) return;
    const int value = std::clamp(id.mid(9).toInt(), 0, kSpawnBase - 1);
    if (value != metatile_) {
        metatile_ = value;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int DoraemonRouteModel::cellValue(QPoint cell) const {
    const int value = valueAt(locate(cell));
    return value >= 0 && value < kSpawnBase ? value : -1;
}

bool DoraemonRouteModel::isCellEditable(QPoint cell) const {
    const Located at = locate(cell);
    return at.panel >= 0 && at.raw >= 0 && valueAt(at) < kSpawnBase;
}

QString DoraemonRouteModel::lockReason(QPoint cell) const {
    const Located at = locate(cell);
    if (at.panel < 0) return QStringLiteral("No screen of the route lies here.");
    if (at.raw < 0) return QStringLiteral("Stream padding: the screen has no cell here.");
    return QStringLiteral("An enemy spawn: its place in the stream stays as it is.");
}

bool DoraemonRouteModel::commit(const QHash<QPair<int, int>, int>& cells, const QString& text, QString* error) {
    // Selectors that share a stream show the same cells: they change together.
    const QJsonArray aliases = session_->world2View().value(QStringLiteral("aliases")).toArray();
    return session_->editBundle(
        text,
        [cells, aliases](nes::Bundle& b) {
            QJsonObject r = b.resource(kScreens);
            QJsonArray screens = r.value(QStringLiteral("screens")).toArray();
            for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
                const int rx = it.key().second % 16;
                const int ry = it.key().second / 16;
                QJsonArray targets = aliases.at(it.key().first).toArray();
                if (targets.isEmpty()) targets << it.key().first;
                for (const QJsonValue& t : targets) {
                    QJsonArray screen = screens.at(t.toInt()).toArray();
                    QJsonArray row = screen.at(ry).toArray();
                    row.replace(rx, it.value());
                    screen.replace(ry, row);
                    screens.replace(t.toInt(), screen);
                }
            }
            r.insert(QStringLiteral("screens"), screens);
            b.setResource(kScreens, r);
        },
        error);
}

bool DoraemonRouteModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("metatile:")) || !isCellEditable(cell)) return false;
    setCurrentLibraryItem(libraryItem);
    const Located at = locate(cell);
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * gridSize().width() + cell.x()));
    return commit({{{at.selector, at.raw}, metatile_}}, QStringLiteral("Set a metatile of %1").arg(title()), error);
}

void DoraemonRouteModel::beginStroke() {
    stroke_.clear();
}

bool DoraemonRouteModel::paintCell(QPoint cell, int value) {
    if (value < 0 || value >= kSpawnBase || !isCellEditable(cell)) return false;
    const Located at = locate(cell);
    if (valueAt(at) == value) return false;
    stroke_.insert({at.selector, at.raw}, value);
    emit contentChanged();
    return true;
}

void DoraemonRouteModel::endStroke() {
    if (stroke_.isEmpty()) return;
    const QHash<QPair<int, int>, int> painted = stroke_;
    stroke_.clear();
    QString error;
    if (!commit(painted, QStringLiteral("Paint %1").arg(title()), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonRouteModel::objects() const {
    QVector<sdk::MapObject> result;
    const int width = gridSize().width();
    for (const Panel& p : panels_) {
        for (int local = 0; local < kPanelWidth * kPanelHeight; ++local) {
            const int cx = p.x * kPanelWidth + local % kPanelWidth;
            const int cy = p.y * kPanelHeight + local / kPanelWidth;
            sdk::MapObject o;
            o.id = QStringLiteral("cell:%1").arg(cy * width + cx);
            o.label = QStringLiteral("Screen %1 · %2, %3").arg(nes::hex(p.selector)).arg(local % kPanelWidth).arg(local / kPanelWidth);
            o.cells = QRect(cx, cy, 1, 1);
            o.movable = false;
            o.outlined = false;
            result << o;
        }
    }
    return result;
}

QString DoraemonRouteModel::selectedObject() const {
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void DoraemonRouteModel::selectObject(const QString& id) {
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

bool DoraemonRouteModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (cell_ < 0 || key != QLatin1String("metatile")) return TileMapModel::setField(key, value, error);
    const QPoint cell(cell_ % gridSize().width(), cell_ / gridSize().width());
    if (!isCellEditable(cell)) {
        if (error) *error = lockReason(cell);
        return false;
    }
    const Located at = locate(cell);
    return commit({{{at.selector, at.raw}, std::clamp(value.toInt(), 0, kSpawnBase - 1)}},
                  QStringLiteral("Set a metatile of %1").arg(title()), error);
}

sdk::Library DoraemonRouteModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = QStringLiteral("click: brush · drag onto the route");
    int preset = panels_.isEmpty() ? 0 : panels_.first().palette;
    if (cell_ >= 0) {
        const Located at = locate(QPoint(cell_ % gridSize().width(), cell_ / gridSize().width()));
        if (at.panel >= 0) preset = panels_.at(at.panel).palette;
    }
    for (int m = 0; m < kSpawnBase; ++m) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("metatile:%1").arg(m);
        item.label = nes::hex(m);
        item.icon = session_->painter().metatile(m, preset);
        result.items << item;
    }
    return result;
}

sdk::Budget DoraemonRouteModel::budget() const {
    sdk::Budget budget;
    budget.text = QStringLiteral("%1 screens · streams repacked into 16431 bytes on build").arg(panels_.size());
    return budget;
}

sdk::Inspection DoraemonRouteModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.world2-screens.v1");
    const QPoint cell = cell_ >= 0 ? QPoint(cell_ % gridSize().width(), cell_ / gridSize().width()) : QPoint(-1, -1);
    const Located at = locate(cell);
    if (at.panel < 0) {
        inspection.kicker = QStringLiteral("World 2 route");
        inspection.title = title();
        inspection.note = QStringLiteral("The screens of this part of the stage in the order the stage bytecode scrolls "
                                         "them, each in its background preset. Paint metatiles onto background cells; "
                                         "red cells are enemy spawns. Screens that share one stream change together, "
                                         "and the build repacks the streams into their region.");
        return inspection;
    }
    const Panel& panel = panels_.at(at.panel);
    const int value = valueAt(at);
    inspection.kicker = QStringLiteral("Screen %1").arg(nes::hex(at.selector));
    inspection.title = QStringLiteral("%1 · %2, %3").arg(title()).arg(cell.x()).arg(cell.y());
    sdk::Field field;
    field.key = QStringLiteral("metatile");
    field.label = QStringLiteral("Metatile");
    field.value = value;
    field.monospace = true;
    if (isCellEditable(cell)) {
        field.type = sdk::Field::Type::Integer;
        field.minimum = 0;
        field.maximum = kSpawnBase - 1;
        field.hint = nes::hex(value);
    } else {
        field.hint = value >= kPadding ? QStringLiteral("padding") : QStringLiteral("enemy state %1").arg(nes::hex(value - kSpawnBase));
    }
    sdk::Field screen;
    screen.key = QStringLiteral("screen");
    screen.label = QStringLiteral("Screen");
    screen.value = QStringLiteral("%1 · scrolls %2 · preset %3")
                       .arg(nes::hex(at.selector))
                       .arg(QLatin1String(kDirections[panel.direction & 3]))
                       .arg(panel.palette);
    inspection.fields = {field, screen};
    const int shared = session_->world2View().value(QStringLiteral("aliases")).toArray().at(at.selector).toArray().size();
    inspection.note = isCellEditable(cell)
                          ? (shared > 1 ? QStringLiteral("%1 screens share this stream and change together.").arg(shared)
                                        : QStringLiteral("A background cell of this screen."))
                          : lockReason(cell);
    return inspection;
}

QString DoraemonRouteModel::statusText() const {
    if (cell_ < 0) return QStringLiteral("World 2 · %1").arg(title());
    return QStringLiteral("World 2 · %1 · %2, %3").arg(title()).arg(cell_ % gridSize().width()).arg(cell_ / gridSize().width());
}

}  // namespace rl::doraemon
