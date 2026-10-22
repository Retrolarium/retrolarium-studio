#include "doraemon/plugin/route_model.hpp"

#include "doraemon/plugin/animation_model.hpp"
#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace {

constexpr int kPanelWidth = 16;
constexpr int kPanelHeight = 15;
const QString kScreens = QStringLiteral("world2-screens");
const QString kSecrets = QStringLiteral("world2-secret-screens");

// Spawn tokens $D0-$DE start enemy states $01-$0F. Each state's name and
// first metasprite are the game's (world2_enemies and the
// world2_state_metasprites catalog); a state the game never draws (its
// render handler is an RTS) is in world2_invisible_states.
constexpr int kStates = 15;

int stateMetasprite(int state) {
    const QJsonValue value = sdk::gameCatalog("world2_state_metasprites").toObject().value(QString::number(state));
    return value.isDouble() ? value.toInt() : -1;
}

bool invisibleState(int state) {
    for (const QJsonValue& value : sdk::gameCatalog("world2_invisible_states").toArray()) {
        if (value.toInt() == state) return true;
    }
    return false;
}

int stateOf(int value) { return value - DoraemonRouteModel::kSpawnBase + 1; }

QString foeName(int state) {
    return sdk::gameName("world2_enemies", state, rlTr("doraemon", "State %1").arg(nes::hex(state)));
}

QString foeChoice(int state) { return QStringLiteral("%1 %2").arg(nes::hex(state)).arg(foeName(state)); }

QImage foeImage(const DoraemonSession* session, int state, int preset) {
    const int metasprite = state >= 1 && state <= kStates ? stateMetasprite(state) : -1;
    if (metasprite >= 0) return world2Image(session, metasprite, preset);
    if (invisibleState(state)) {
        // Invisible in the game: a dashed, see-through outline marks where it is.
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor(150, 210, 255, 60));
        QPainter painter(&image);
        QPen pen(QColor(150, 210, 255, 230), 1, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRect(image.rect().adjusted(0, 0, -1, -1));
        QFont font = painter.font();
        font.setPixelSize(9);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("B"));
        return image;
    }
    // No sprite of its own: a neutral marker with the state.
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(QColor(120, 120, 120, 170));
    QPainter painter(&image);
    QFont font = painter.font();
    font.setPixelSize(9);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, QString::number(state, 16).toUpper());
    return image;
}

// The symbol of a screen that can drop an inventory item: the ROM holds
// neither the item nor a place, so it sits in the screen's corner,
// translucent through a checkerboard.
QImage secretSymbol() {
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(255, 205, 70));
    painter.setPen(QPen(QColor(120, 70, 0), 2));
    painter.drawEllipse(QRectF(2, 2, 20, 20));
    QFont font = painter.font();
    font.setPixelSize(15);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("?"));
    painter.end();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            c.setAlpha((x + y) % 2 ? 0 : c.alpha() * 3 / 4);
            image.setPixelColor(x, y, c);
        }
    }
    return image;
}

constexpr int kSecretColumn = 14;  // the symbol's cells in a panel

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
    const int stage = part.startsWith(QLatin1Char('3')) ? 2 : part.startsWith(QLatin1Char('2')) ? 1 : 0;
    session_->setStartStage(stage);
    chapter_ = 6 + stage;
}

QString DoraemonRouteModel::startPoint() const { return session_->selectedStart(); }

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

QString DoraemonRouteModel::title() const { return name_; }

QSize DoraemonRouteModel::gridSize() const { return {columns_ * kPanelWidth, rows_ * kPanelHeight}; }

DoraemonRouteModel::Located DoraemonRouteModel::locate(QPoint cell) const {
    Located result;
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= columns_ * kPanelWidth || cell.y() >= rows_ * kPanelHeight)
        return result;
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

QVector<sdk::MapLayer> DoraemonRouteModel::layers() const {
    return {{QStringLiteral("blocks"), tr("Metatiles")}, {QStringLiteral("objects"), tr("Objects")}};
}

void DoraemonRouteModel::setCurrentLayer(const QString& id) {
    if (id == layer_ || (id != QLatin1String("blocks") && id != QLatin1String("objects"))) return;
    layer_ = id;
    emit toolsChanged();
    emit selectionChanged();
    emit contentChanged();
}

QVector<int> DoraemonRouteModel::secretScreens() const {
    QVector<int> result;
    for (const QJsonValue& v : session_->resource(kSecrets).value(QStringLiteral("screens")).toArray())
        result << v.toInt();
    return result;
}

QVector<sdk::Tool> DoraemonRouteModel::tools() const {
    if (objectLayer()) {
        return {
            {QStringLiteral("select"), tr("Select and move enemies"), QStringLiteral("select")},
            {QStringLiteral("place"), tr("Add the sheet's enemy on a background cell"), QStringLiteral("place")},
            {QStringLiteral("remove"), tr("Remove an enemy (its cell becomes metatile $00)"), QStringLiteral("erase")}};
    }
    return {
        {QStringLiteral("select"), tr("Select a cell"), QStringLiteral("select")},
        {QStringLiteral("paint"), tr("Paint the sheet's metatile (right button takes one)"), QStringLiteral("paint")},
        {QStringLiteral("pick"), tr("Take a cell's metatile as the brush"), QStringLiteral("pick")}};
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
                // An enemy spawn: the empty metatile under its sprite, in the
                // chapter's sprite colours.
                painter.drawImage(at, art.metatile(0, p.palette));
                painter.drawImage(at, foeImage(session_, stateOf(value), chapter_));
                if (!objectLayer()) {
                    painter.setPen(QColor(230, 60, 60, 170));
                    painter.drawRect(QRect(at, QSize(15, 15)));
                }
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
    for (const Panel& p : panels_)
        painter.drawRect(p.x * kPanelWidth * 16, p.y * kPanelHeight * 16, kPanelWidth * 16 - 1, kPanelHeight * 16 - 1);
    const QVector<int> secrets = secretScreens();
    const QImage symbol = secretSymbol();
    for (const Panel& p : panels_) {
        if (secrets.contains(p.selector)) {
            painter.drawImage((p.x * kPanelWidth + kSecretColumn) * 16 + 4, p.y * kPanelHeight * 16 + 4, symbol);
        }
    }
    font.setPixelSize(14);
    painter.setFont(font);
    for (const QJsonObject& a : annotations_) {
        const QRect box(a.value(QStringLiteral("x")).toInt() * kPanelWidth * 16 + 8,
                        a.value(QStringLiteral("y")).toInt() * kPanelHeight * 16 + 8, 200, 22);
        painter.fillRect(box, QColor(0, 0, 0, 170));
        painter.setPen(QColor(255, 220, 120));
        painter.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         a.value(QStringLiteral("text")).toString());
    }
    return image;
}

QVector<sdk::CellBrush> DoraemonRouteModel::brushes() const {
    if (objectLayer()) return {};
    return {{metatile_, tr("Metatile %1").arg(nes::hex(metatile_)), QColor()}};
}

void DoraemonRouteModel::setCurrentBrush(int value) {
    if (value < 0 || value >= kSpawnBase) return;
    metatile_ = value;
    emit toolsChanged();
    emit selectionChanged();
}

QString DoraemonRouteModel::currentLibraryItem() const {
    if (objectLayer()) return QStringLiteral("enemy:%1").arg(enemy_);
    return QStringLiteral("metatile:%1").arg(metatile_);
}

void DoraemonRouteModel::setCurrentLibraryItem(const QString& id) {
    if (id.startsWith(QLatin1String("enemy:"))) {
        const int state = std::clamp(id.mid(6).toInt(), 1, kStates);
        if (state != enemy_) {
            enemy_ = state;
            emit toolsChanged();
            emit selectionChanged();
        }
        return;
    }
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
    if (at.panel < 0) return tr("No screen of the route lies here.");
    if (at.raw < 0) return tr("Stream padding: the screen has no cell here.");
    return tr("An enemy spawn: move or change it in the Objects layer.");
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
    if (libraryItem.startsWith(QLatin1String("enemy:"))) {
        if (!isCellEditable(cell)) {
            if (error) *error = lockReason(cell);
            return false;
        }
        setCurrentLibraryItem(libraryItem);
        const Located at = locate(cell);
        const int index = cell.y() * gridSize().width() + cell.x();
        if (!commit({{{at.selector, at.raw}, kSpawnBase + enemy_ - 1}},
                    tr("Add %1 to %2").arg(foeName(enemy_)).arg(title()), error)) {
            return false;
        }
        selectObject(QStringLiteral("spawn:%1").arg(index));
        return true;
    }
    if (!libraryItem.startsWith(QLatin1String("metatile:")) || !isCellEditable(cell)) return false;
    setCurrentLibraryItem(libraryItem);
    const Located at = locate(cell);
    selectObject(QStringLiteral("cell:%1").arg(cell.y() * gridSize().width() + cell.x()));
    return commit({{{at.selector, at.raw}, metatile_}}, tr("Set a metatile of %1").arg(title()), error);
}

void DoraemonRouteModel::beginStroke() { stroke_.clear(); }

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
    if (!commit(painted, tr("Paint %1").arg(title()), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonRouteModel::objects() const {
    QVector<sdk::MapObject> result;
    const int width = gridSize().width();
    if (objectLayer()) {
        for (const Panel& p : panels_) {
            for (int local = 0; local < kPanelWidth * kPanelHeight; ++local) {
                if (p.raw.at(local) < 0) continue;
                const int cx = p.x * kPanelWidth + local % kPanelWidth;
                const int cy = p.y * kPanelHeight + local / kPanelWidth;
                const int value = valueAt(locate(QPoint(cx, cy)));
                if (value < kSpawnBase || value >= kPadding) continue;
                // While dragging, the moved spawn keeps its id.
                const int index = dragFrom_ >= 0 && cy * width + cx == dragTo_ ? dragFrom_ : cy * width + cx;
                sdk::MapObject o;
                o.id = QStringLiteral("spawn:%1").arg(index);
                o.label = tr("%1 · screen %2").arg(foeName(stateOf(value))).arg(nes::hex(p.selector));
                o.cells = QRect(cx, cy, 1, 1);
                result << o;
            }
        }
        const QVector<int> secrets = secretScreens();
        for (int i = 0; i < panels_.size(); ++i) {
            const Panel& p = panels_.at(i);
            const int slot = secrets.indexOf(p.selector);
            if (slot < 0) continue;
            sdk::MapObject o;
            o.id = QStringLiteral("secret:%1:%2").arg(slot).arg(i);
            o.label = tr("Secret item · screen %1").arg(nes::hex(p.selector));
            o.cells = QRect(p.x * kPanelWidth + kSecretColumn, p.y * kPanelHeight, 2, 2);
            o.movable = false;
            result << o;
        }
        return result;
    }
    for (const Panel& p : panels_) {
        for (int local = 0; local < kPanelWidth * kPanelHeight; ++local) {
            const int cx = p.x * kPanelWidth + local % kPanelWidth;
            const int cy = p.y * kPanelHeight + local / kPanelWidth;
            sdk::MapObject o;
            o.id = QStringLiteral("cell:%1").arg(cy * width + cx);
            o.label =
                tr("Screen %1 · %2, %3").arg(nes::hex(p.selector)).arg(local % kPanelWidth).arg(local / kPanelWidth);
            o.cells = QRect(cx, cy, 1, 1);
            o.movable = false;
            o.outlined = false;
            result << o;
        }
    }
    return result;
}

QString DoraemonRouteModel::selectedObject() const {
    if (objectLayer()) return object_;
    return cell_ >= 0 ? QStringLiteral("cell:%1").arg(cell_) : QString();
}

void DoraemonRouteModel::selectObject(const QString& id) {
    if (objectLayer()) {
        if (id != object_) {
            object_ = id;
            emit selectionChanged();
        }
        return;
    }
    const int cell = id.startsWith(QLatin1String("cell:")) ? id.mid(5).toInt() : -1;
    if (cell != cell_) {
        cell_ = cell;
        emit selectionChanged();
    }
}

void DoraemonRouteModel::beginDrag(const QString& id) {
    if (!objectLayer() || !id.startsWith(QLatin1String("spawn:"))) return;
    dragFrom_ = id.mid(6).toInt();
    dragTo_ = dragFrom_;
    stroke_.clear();
}

bool DoraemonRouteModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (dragFrom_ < 0 || id != QStringLiteral("spawn:%1").arg(dragFrom_)) return false;
    const int target = topLeftCell.y() * gridSize().width() + topLeftCell.x();
    if (target == dragTo_) return false;
    // The spawn moves onto a background cell and leaves the empty
    // metatile it showed; the stream is repacked on save.
    const QHash<QPair<int, int>, int> previous = stroke_;
    stroke_.clear();
    if (target != dragFrom_) {
        const Located from = locate(cellOf(dragFrom_));
        const Located to = locate(topLeftCell);
        const int token = valueAt(from);
        if (to.panel < 0 || to.raw < 0 || valueAt(to) >= kSpawnBase ||
            (to.selector == from.selector && to.raw == from.raw)) {
            stroke_ = previous;
            return false;
        }
        stroke_.insert({from.selector, from.raw}, 0);
        stroke_.insert({to.selector, to.raw}, token);
    }
    dragTo_ = target;
    emit contentChanged();
    return true;
}

void DoraemonRouteModel::endDrag() {
    if (dragFrom_ < 0) return;
    const QHash<QPair<int, int>, int> moved = stroke_;
    const Located to = locate(cellOf(dragTo_));
    const int index = dragTo_;
    dragFrom_ = dragTo_ = -1;
    stroke_.clear();
    if (!moved.isEmpty()) {
        object_ = QStringLiteral("spawn:%1").arg(index);
        QString error;
        if (!commit(moved, tr("Move %1 on %2").arg(foeName(stateOf(moved.value({to.selector, to.raw})))).arg(title()),
                    &error)) {
            session_->report(sdk::Severity::Error, QStringLiteral("levels"), error);
        }
        emit selectionChanged();
    }
    emit contentChanged();
}

bool DoraemonRouteModel::removeObject(const QString& id, QString* error) {
    if (!objectLayer() || !id.startsWith(QLatin1String("spawn:"))) {
        if (error && id.startsWith(QLatin1String("secret:"))) {
            *error =
                tr("The secret item table always holds seven screens: give this one another screen in the inspector.");
        }
        return false;
    }
    const Located at = locate(cellOf(id.mid(6).toInt()));
    const int value = valueAt(at);
    if (at.raw < 0 || value < kSpawnBase || value >= kPadding) return false;
    if (object_ == id) object_.clear();
    emit selectionChanged();
    return commit({{{at.selector, at.raw}, 0}}, tr("Remove %1 from %2").arg(foeName(stateOf(value))).arg(title()),
                  error);
}

sdk::Inspection DoraemonRouteModel::inspectObject() const {
    sdk::Inspection inspection;
    if (object_.startsWith(QLatin1String("secret:"))) {
        const int slot = object_.section(QLatin1Char(':'), 1, 1).toInt();
        const int selector = secretScreens().value(slot, -1);
        inspection.engineTag = QStringLiteral("doraemon.world2-secret-screens.v1");
        inspection.kicker = tr("Secret item");
        inspection.title = tr("Screen %1").arg(nes::hex(selector));
        sdk::Field screen;
        screen.key = QStringLiteral("secret");
        screen.label = tr("Screen");
        screen.type = sdk::Field::Type::Integer;
        screen.minimum = 0;
        screen.maximum =
            std::max(0, int(session_->resource(kScreens).value(QStringLiteral("screens")).toArray().size()) - 1);
        screen.value = selector;
        screen.hint = nes::hex(selector);
        screen.monospace = true;
        sdk::Field place;
        place.key = QStringLiteral("slot");
        place.label = tr("Table slot");
        place.value = tr("%1 of 7").arg(slot + 1);
        inspection.fields = {screen, place};
        inspection.note =
            tr("While Doraemon flies through one of these seven screens, enough hits drop an "
               "inventory item (it rises from X $78, Y $F0). The ROM picks neither the item "
               "nor a place, so the symbol only marks the screen. Set another screen number "
               "to move this secret there.");
        return inspection;
    }
    inspection.engineTag = QStringLiteral("doraemon.world2-screens.v1");
    const Located at = object_.startsWith(QLatin1String("spawn:")) ? locate(cellOf(object_.mid(6).toInt())) : Located{};
    const int value = valueAt(at);
    if (at.panel < 0 || value < kSpawnBase || value >= kPadding) {
        inspection.kicker = tr("Objects");
        inspection.title = title();
        inspection.note =
            tr("Enemy spawns sit in the screen streams: each starts its enemy when the screen "
               "scrolls in. Drag one onto another cell, change its enemy here, add one from "
               "the sheet or delete one. Translucent ? symbols mark the screens that can drop "
               "a secret item.");
        return inspection;
    }
    const int state = stateOf(value);
    const QPoint cell = cellOf(object_.mid(6).toInt());
    inspection.kicker = invisibleState(state) ? tr("Enemy spawn · invisible in the game") : tr("Enemy spawn");
    inspection.title = foeName(state);
    sdk::Field enemy;
    enemy.key = QStringLiteral("enemy");
    enemy.label = tr("Enemy");
    enemy.type = sdk::Field::Type::Choice;
    for (int s = 1; s <= kStates; ++s) enemy.choices << foeChoice(s);
    enemy.value = foeChoice(state);
    sdk::Field screen;
    screen.key = QStringLiteral("screen");
    screen.label = tr("Screen");
    screen.value =
        tr("%1 · cell %2, %3").arg(nes::hex(at.selector)).arg(cell.x() % kPanelWidth).arg(cell.y() % kPanelHeight);
    inspection.fields = {enemy, screen};
    const int shared =
        session_->world2View().value(QStringLiteral("aliases")).toArray().at(at.selector).toArray().size();
    inspection.note =
        tr("Token %1 in the screen's stream; the enemy appears when the screen scrolls in.%2%3")
            .arg(nes::hex(value))
            .arg(invisibleState(state) ? tr(" The background draws this enemy, so it shows as a marker.") : QString())
            .arg(shared > 1 ? tr(" %1 screens share this stream and change together.").arg(shared) : QString());
    return inspection;
}

bool DoraemonRouteModel::setObjectField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("secret") && object_.startsWith(QLatin1String("secret:"))) {
        const int slot = object_.section(QLatin1Char(':'), 1, 1).toInt();
        const int selector = value.toInt();
        const QVector<int> secrets = secretScreens();
        if (slot < 0 || slot >= secrets.size() || secrets.at(slot) == selector) return false;
        if (secrets.contains(selector)) {
            if (error) *error = tr("Screen %1 already drops a secret item.").arg(nes::hex(selector));
            return false;
        }
        // Follow the symbol to the first panel showing that screen.
        for (int i = 0; i < panels_.size(); ++i) {
            if (panels_.at(i).selector == selector) {
                object_ = QStringLiteral("secret:%1:%2").arg(slot).arg(i);
                break;
            }
        }
        return session_->editBundle(
            tr("Move a secret item to screen %1").arg(nes::hex(selector)),
            [slot, selector](nes::Bundle& b) {
                QJsonObject r = b.resource(kSecrets);
                QJsonArray list = r.value(QStringLiteral("screens")).toArray();
                list.replace(slot, selector);
                r.insert(QStringLiteral("screens"), list);
                b.setResource(kSecrets, r);
            },
            error);
    }
    if (key == QLatin1String("enemy") && object_.startsWith(QLatin1String("spawn:"))) {
        const Located at = locate(cellOf(object_.mid(6).toInt()));
        const int state = value.toString().section(QLatin1Char(' '), 0, 0).remove(QLatin1Char('$')).toInt(nullptr, 16);
        if (at.raw < 0 || state < 1 || state > kStates || valueAt(at) == kSpawnBase + state - 1) return false;
        return commit({{{at.selector, at.raw}, kSpawnBase + state - 1}},
                      tr("Set an enemy of %1 to %2").arg(title()).arg(foeName(state)), error);
    }
    return TileMapModel::setField(key, value, error);
}

bool DoraemonRouteModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (objectLayer()) return setObjectField(key, value, error);
    if (cell_ < 0 || key != QLatin1String("metatile")) return TileMapModel::setField(key, value, error);
    const QPoint cell(cell_ % gridSize().width(), cell_ / gridSize().width());
    if (!isCellEditable(cell)) {
        if (error) *error = lockReason(cell);
        return false;
    }
    const Located at = locate(cell);
    return commit({{{at.selector, at.raw}, std::clamp(value.toInt(), 0, kSpawnBase - 1)}},
                  tr("Set a metatile of %1").arg(title()), error);
}

sdk::Library DoraemonRouteModel::sheet() const {
    sdk::Library result;
    if (objectLayer()) {
        result.grid = true;
        result.columns = 5;
        result.hint = tr("click: the enemy to add · drag onto the route");
        for (int s = 1; s <= kStates; ++s) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("enemy:%1").arg(s);
            item.label = foeChoice(s);
            item.icon = foeImage(session_, s, chapter_);
            result.items << item;
        }
        return result;
    }
    result.grid = true;
    result.columns = 16;
    result.hint = tr("click: brush · drag onto the route");
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
    budget.text = tr("%1 screens · streams repacked into 16431 bytes on build").arg(panels_.size());
    return budget;
}

sdk::Inspection DoraemonRouteModel::inspect() const {
    if (objectLayer()) return inspectObject();
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("doraemon.world2-screens.v1");
    const QPoint cell = cell_ >= 0 ? QPoint(cell_ % gridSize().width(), cell_ / gridSize().width()) : QPoint(-1, -1);
    const Located at = locate(cell);
    if (at.panel < 0) {
        inspection.kicker = tr("World 2 route");
        inspection.title = title();
        inspection.note =
            tr("The screens of this part of the stage in the order the stage bytecode scrolls "
               "them, each in its background preset. Paint metatiles onto background cells; "
               "framed enemies are spawns, edited in the Objects layer. Screens that share one "
               "stream change together, "
               "and the build repacks the streams into their region.");
        return inspection;
    }
    const Panel& panel = panels_.at(at.panel);
    const int value = valueAt(at);
    inspection.kicker = tr("Screen %1").arg(nes::hex(at.selector));
    inspection.title = QStringLiteral("%1 · %2, %3").arg(title()).arg(cell.x()).arg(cell.y());
    sdk::Field field;
    field.key = QStringLiteral("metatile");
    field.label = tr("Metatile");
    field.value = value;
    field.monospace = true;
    if (isCellEditable(cell)) {
        field.type = sdk::Field::Type::Integer;
        field.minimum = 0;
        field.maximum = kSpawnBase - 1;
        field.hint = nes::hex(value);
    } else {
        field.hint = value >= kPadding ? QStringLiteral("padding") : foeChoice(stateOf(value));
    }
    sdk::Field screen;
    screen.key = QStringLiteral("screen");
    screen.label = tr("Screen");
    screen.value = tr("%1 · scrolls %2 · preset %3")
                       .arg(nes::hex(at.selector))
                       .arg(QLatin1String(kDirections[panel.direction & 3]))
                       .arg(panel.palette);
    inspection.fields = {field, screen};
    const int shared =
        session_->world2View().value(QStringLiteral("aliases")).toArray().at(at.selector).toArray().size();
    inspection.note = isCellEditable(cell)
                          ? (shared > 1 ? tr("%1 screens share this stream and change together.").arg(shared)
                                        : tr("A background cell of this screen."))
                          : lockReason(cell);
    return inspection;
}

QString DoraemonRouteModel::statusText() const {
    if (cell_ < 0) return tr("World 2 · %1").arg(title());
    return tr("World 2 · %1 · %2, %3").arg(title()).arg(cell_ % gridSize().width()).arg(cell_ / gridSize().width());
}

}  // namespace rl::doraemon
