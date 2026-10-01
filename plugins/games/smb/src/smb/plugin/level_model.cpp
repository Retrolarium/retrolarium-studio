#include "smb/plugin/level_model.hpp"

#include "smb/data/catalog.hpp"
#include "smb/plugin/smb_session.hpp"

#include <QPainter>

#include <algorithm>

namespace rl::smb {

namespace {

QString hex(int value) {
    return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

// Cells a pixel rectangle covers, kept inside the play field.
QRect cellsOf(const QRect& pixels) {
    const int left = std::max(0, pixels.left()) / 16;
    const int top = std::max(0, pixels.top()) / 16;
    const int right = std::max(0, pixels.right()) / 16;
    const int bottom = std::min(12, std::max(0, pixels.bottom()) / 16);
    return QRect(QPoint(left, top), QPoint(std::max(left, right), std::max(top, bottom)));
}

sdk::Field readOnly(const QString& key, const QString& label, const QVariant& value, bool mono = false) {
    sdk::Field field;
    field.key = key;
    field.label = label;
    field.value = value;
    field.monospace = mono;
    return field;
}

const QStringList& timers() {
    static const QStringList kTimers{QStringLiteral("none"), QStringLiteral("400"), QStringLiteral("300"),
                                     QStringLiteral("200")};
    return kTimers;
}

}  // namespace

SmbLevelModel::SmbLevelModel(SmbSession* session, const QString& area)
    : TileMapModel(session), session_(session), area_(area) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &SmbSession::startChanged, this, [this] {
        emit contentChanged();
        emit toolsChanged();
    });
}

const SmbLevelScene& SmbLevelModel::scene() const {
    if (sceneRevision_ != session_->revision()) {
        const SmbResourceDocument& d = session_->document();
        const SmbArea area = d.area(session_->levels(), area_);
        const QPair<int, int> course = session_->courseOf(area_);
        scene_ = renderSmbLevelScene(area_, area.header, area.objects, area.enemies,
                                     std::max(0, course.first - 1),
                                     SmbLevelRendering::fromId(d.rendering(session_->levels())));
        sceneRevision_ = session_->revision();
    }
    return scene_;
}

QString SmbLevelModel::title() const {
    QString type = areaTypeOf(area_);
    if (!type.isEmpty()) {
        type[0] = type.at(0).toUpper();
    }
    return QStringLiteral("%1 %2").arg(type, area_.section(QLatin1Char('_'), -1));
}

QSize SmbLevelModel::gridSize() const {
    return {std::max(16, scene().width), SmbLevelPainter::kRows};
}

QVector<sdk::Tool> SmbLevelModel::tools() const {
    return {{QStringLiteral("select"), QStringLiteral("Select, and drag Hero to the start point"),
             QStringLiteral("select")}};
}

QImage SmbLevelModel::render() const {
    const QString type = areaTypeOf(area_);
    QImage image = session_->painter().render(scene(), type, session_->night(area_));
    const QImage hero = session_->painter().hero();
    if (!hero.isNull()) {
        QPainter painter(&image);
        painter.drawImage(session_->painter().heroBounds(session_->heroCell(area_)).topLeft(), hero);
    }
    return image;
}

QVector<sdk::MapObject> SmbLevelModel::objects() const {
    QVector<sdk::MapObject> result;
    const QString type = areaTypeOf(area_);
    for (const SmbPositionedObject& object : scene().objects) {
        sdk::MapObject o;
        o.id = QStringLiteral("object:%1").arg(object.index);
        o.label = areaObjectName(object.record.row, object.record.control);
        const int row = std::min(object.record.row, 12);
        o.cells = QRect(object.x, row, std::max(1, object.width), 1);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    for (const SmbPositionedEnemy& enemy : scene().enemies) {
        if (enemy.record.kind != QLatin1String("enemy")) {
            continue;
        }
        sdk::MapObject o;
        o.id = QStringLiteral("enemy:%1").arg(enemy.index);
        o.label = enemyName(enemy.record.objectOrPage);
        o.cells = cellsOf(session_->painter().enemyBounds(enemy, type));
        o.movable = false;
        result << o;
    }
    sdk::MapObject hero;
    hero.id = QStringLiteral("hero");
    hero.label = QStringLiteral("Hero · start point");
    hero.cells = cellsOf(session_->painter().heroBounds(session_->heroCell(area_)));
    hero.movable = true;
    result << hero;
    return result;
}

void SmbLevelModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void SmbLevelModel::beginDrag(const QString& id) {
    selectObject(id);
    if (id == QLatin1String("hero")) {
        const QPoint feet = session_->heroCell(area_);
        const QRect cells = cellsOf(session_->painter().heroBounds(feet));
        dragOffset_ = feet - cells.topLeft();
    }
}

bool SmbLevelModel::dragObject(const QString& id, QPoint topLeftCell) {
    if (id != QLatin1String("hero")) {
        return false;
    }
    // The start point is not level data: it moves without an undo step.
    const QPoint feet(std::clamp(topLeftCell.x() + dragOffset_.x(), 0, std::max(0, scene().width - 1)),
                      std::clamp(topLeftCell.y() + dragOffset_.y(), 1, 12));
    if (feet == session_->heroCell(area_)) {
        return false;
    }
    session_->setHeroCell(area_, feet);
    return true;
}

void SmbLevelModel::endDrag() {}

QString SmbLevelModel::startPoint() const {
    const SmbStart start = session_->start();
    if (start.area != area_) {
        return {};
    }
    return QStringLiteral("World %1-%2 · column %3").arg(start.world).arg(start.course).arg(start.cell.x());
}

sdk::Budget SmbLevelModel::budget() const {
    const SmbArea area = session_->document().area(session_->levels(), area_);
    sdk::Budget budget;
    budget.used = area.areaBytes();
    budget.capacity = area.areaCapacity + (area.areaImplicitTerminator ? 1 : 0);
    budget.text = QStringLiteral("objects %1 / %2 · enemies %3 / %4 bytes")
                      .arg(area.areaBytes())
                      .arg(area.areaCapacity)
                      .arg(area.enemyBytes())
                      .arg(area.enemyCapacity);
    return budget;
}

QVector<sdk::ViewOption> SmbLevelModel::viewOptions() const {
    sdk::ViewOption lighting;
    lighting.key = QStringLiteral("lighting");
    lighting.label = QStringLiteral("Lighting");
    lighting.value = session_->lighting();
    lighting.choices = QStringList{QStringLiteral("Auto"), QStringLiteral("Day"), QStringLiteral("Night")};
    return {lighting};
}

void SmbLevelModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("lighting")) {
        session_->setLighting(value);
        emit toolsChanged();
    }
}

QString SmbLevelModel::statusText() const {
    const QPair<int, int> course = session_->courseOf(area_);
    return course.first > 0 ? QStringLiteral("%1 · World %2-%3 · %4 columns")
                                  .arg(title())
                                  .arg(course.first)
                                  .arg(course.second)
                                  .arg(scene().width)
                            : QStringLiteral("%1 · %2 columns").arg(title()).arg(scene().width);
}

sdk::Inspection SmbLevelModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("smb.levels.v1");
    const SmbArea area = session_->document().area(session_->levels(), area_);
    if (selected_.startsWith(QLatin1String("object:"))) {
        const int index = selected_.mid(7).toInt();
        if (index >= 0 && index < area.objects.size()) {
            const SmbAreaObject& o = area.objects.at(index);
            inspection.kicker = QStringLiteral("Area object");
            inspection.title = areaObjectName(o.row, o.control);
            inspection.fields = {readOnly(QStringLiteral("column"), QStringLiteral("Column"), o.column),
                                 readOnly(QStringLiteral("row"), QStringLiteral("Row"), o.row),
                                 readOnly(QStringLiteral("control"), QStringLiteral("Control"), hex(o.control), true),
                                 readOnly(QStringLiteral("page"), QStringLiteral("Next page"),
                                          o.pageAdvance ? QStringLiteral("yes") : QStringLiteral("no"))};
            inspection.engine = {{QStringLiteral("Record"), QStringLiteral("%1 of %2").arg(index).arg(area.objects.size())},
                                 {QStringLiteral("Bytes"), hex((o.column << 4) | o.row) + QLatin1Char(' ') +
                                                               hex((o.pageAdvance ? 0x80 : 0) | o.control)}};
            return inspection;
        }
    }
    if (selected_.startsWith(QLatin1String("enemy:"))) {
        const int index = selected_.mid(6).toInt();
        if (index >= 0 && index < area.enemies.size()) {
            const SmbEnemyRecord& e = area.enemies.at(index);
            inspection.kicker = QStringLiteral("Enemy");
            inspection.title = enemyName(e.objectOrPage);
            inspection.fields = {readOnly(QStringLiteral("column"), QStringLiteral("Column"), e.column),
                                 readOnly(QStringLiteral("row"), QStringLiteral("Row"), e.row),
                                 readOnly(QStringLiteral("kind"), QStringLiteral("Enemy"), hex(e.objectOrPage), true),
                                 readOnly(QStringLiteral("hard"), QStringLiteral("Hard mode only"),
                                          e.hardMode ? QStringLiteral("yes") : QStringLiteral("no")),
                                 readOnly(QStringLiteral("page"), QStringLiteral("Next page"),
                                          e.pageAdvance ? QStringLiteral("yes") : QStringLiteral("no"))};
            inspection.engine = {{QStringLiteral("Record"), QStringLiteral("%1 of %2").arg(index).arg(area.enemies.size())}};
            return inspection;
        }
    }
    if (selected_ == QLatin1String("hero")) {
        const SmbStart start = session_->start();
        const QPoint cell = session_->heroCell(area_);
        inspection.kicker = QStringLiteral("Start point");
        inspection.title = QStringLiteral("Hero");
        inspection.fields = {readOnly(QStringLiteral("column"), QStringLiteral("Column"), cell.x()),
                             readOnly(QStringLiteral("row"), QStringLiteral("Row"), cell.y()),
                             readOnly(QStringLiteral("page"), QStringLiteral("Page"), cell.x() / 16),
                             readOnly(QStringLiteral("course"), QStringLiteral("Plays as"),
                                      QStringLiteral("World %1-%2").arg(start.world).arg(start.course))};
        inspection.note = QStringLiteral("Drag Hero to where Play from selected (F5) puts him. The start point "
                                         "is a playtest setting, not level data.");
        return inspection;
    }
    inspection.kicker = QStringLiteral("Area");
    inspection.title = title();
    const QVector<int>& h = area.header;
    inspection.fields = {readOnly(QStringLiteral("timer"), QStringLiteral("Timer"), timers().value(h.value(0))),
                         readOnly(QStringLiteral("entrance"), QStringLiteral("Entrance"), h.value(1)),
                         readOnly(QStringLiteral("foreground"), QStringLiteral("Foreground"), h.value(2)),
                         readOnly(QStringLiteral("style"), QStringLiteral("Style"), h.value(3)),
                         readOnly(QStringLiteral("background"), QStringLiteral("Background"), h.value(4)),
                         readOnly(QStringLiteral("terrain"), QStringLiteral("Terrain"), h.value(5))};
    inspection.note = QStringLiteral("Pick an object, an enemy or Hero. Editing area records comes next; the "
                                     "level is drawn by the same renderer the kit uses.");
    inspection.engine = {{QStringLiteral("Objects"), QStringLiteral("%1 records · %2 / %3 bytes")
                                                         .arg(area.objects.size())
                                                         .arg(area.areaBytes())
                                                         .arg(area.areaCapacity)},
                         {QStringLiteral("Enemies"), QStringLiteral("%1 records · %2 / %3 bytes")
                                                         .arg(area.enemies.size())
                                                         .arg(area.enemyBytes())
                                                         .arg(area.enemyCapacity)}};
    return inspection;
}

}  // namespace rl::smb
