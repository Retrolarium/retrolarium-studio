#include "smb/plugin/world_models.hpp"

#include "sdk/i18n.hpp"
#include "smb/plugin/smb_session.hpp"
#include "smb/render/level_image.hpp"

namespace rl::smb {

namespace {

// "ground_12" -> "Ground 12"
QString areaLabel(const QString& name) {
    QString type = areaTypeOf(name);
    if (!type.isEmpty()) {
        type[0] = type.at(0).toUpper();
    }
    return QStringLiteral("%1 %2").arg(type, name.section(QLatin1Char('_'), -1));
}

// "Ground 6", "ground 6", "ground_6" -> "ground_6"
QString areaName(QString text) {
    text = text.trimmed().toLower();
    text.replace(QLatin1Char(' '), QLatin1Char('_'));
    return text;
}

constexpr int kPhysicsColumns = 7;

struct PhysicsInfo {
    const char* name;
    const char* label;
    const char* meaning;
    QStringList entries;
};

const PhysicsInfo& physicsInfo(const QString& name) {
    static const QStringList jump{QStringLiteral("standing"), QStringLiteral("walking"), QStringLiteral("faster"),
                                  QStringLiteral("running"),  rlTr("smb", "full run"), QStringLiteral("swimming"),
                                  QStringLiteral("whirlpool")};
    static const QStringList speed{QStringLiteral("running"), QStringLiteral("walking"), rlTr("smb", "under water"),
                                   rlTr("smb", "entry 3")};
    static const QStringList climb{QStringLiteral("still"), QStringLiteral("up"), QStringLiteral("down")};
    static const QVector<PhysicsInfo> kInfo{
        {"jump_gravity", "Jump gravity", "Downward pull while A is held and the player rises.", jump},
        {"fall_gravity", "Fall gravity", "Downward pull once A is let go, and while falling.", jump},
        {"initial_y_speed", "Jump speed", "Vertical speed at take-off, in pixels per frame; negative is up.", jump},
        {"initial_y_fraction", "Jump speed fraction", "The sub-pixel part of the take-off speed (256ths).", jump},
        {"maximum_left_speed", "Top speed left", "Fastest walk or run to the left (negative).", speed},
        {"maximum_right_speed", "Top speed right", "Fastest walk or run to the right.", speed},
        {"horizontal_friction", "Friction", "How fast the player speeds up and slows down on the ground.", speed},
        {"climb_y_speed", "Climb speed", "Vertical speed on a vine, in pixels per frame.", climb},
        {"climb_y_fraction", "Climb fraction", "The sub-pixel part of the climb speed (256ths).", climb},
    };
    static const PhysicsInfo kUnknown{"", "", "", {}};
    for (const PhysicsInfo& info : kInfo) {
        if (name == QLatin1String(info.name)) {
            return info;
        }
    }
    return kUnknown;
}

QString physicsLabel(const QString& name) {
    const PhysicsInfo& info = physicsInfo(name);
    if (*info.label) {
        return QString::fromLatin1(info.label);
    }
    QString label = name;
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!label.isEmpty()) label[0] = label.at(0).toUpper();
    return label;
}

QString entryLabel(const QString& name, int index) {
    const QStringList& entries = physicsInfo(name).entries;
    return index < entries.size() ? entries.at(index) : rlTr("smb", "entry %1").arg(index);
}

QString byteHex(int value) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value & 0xff, 2, 16, QLatin1Char('0')).toUpper();
}

}  // namespace

// ---------------------------------------------------------------- routes

SmbRoutesModel::SmbRoutesModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &SmbSession::startChanged, this, &sdk::EditorModel::toolsChanged);
    // Open on the step that plays the current start area.
    for (int row = 0; row < rowCount(); ++row) {
        if (areaAt(row) == session_->start().area) {
            row_ = row;
            break;
        }
    }
}

QPair<int, int> SmbRoutesModel::stepAt(int row) const {
    const SmbResourceDocument& d = session_->document();
    for (int w = 1; w <= d.worldCount(session_->world()); ++w) {
        const int steps = d.routes(session_->world(), w).size();
        if (row < steps) {
            return {w, row};
        }
        row -= steps;
    }
    return {0, -1};
}

QString SmbRoutesModel::areaAt(int row) const {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return {};
    }
    const SmbRoute route = session_->document().routes(session_->world(), at.first).at(at.second);
    const QStringList types = session_->document().areaTypes(session_->levels());
    return QStringLiteral("%1_%2").arg(types.value(route.areaType)).arg(route.areaIndex + 1);
}

QString SmbRoutesModel::courseLabel(int row) const {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return {};
    }
    // The game numbers levels, not steps: a pipe intro (auto-walk entrance)
    // shares the number of the level it leads to.
    const SmbResourceDocument& d = session_->document();
    // A step may name an area the levels lack: it counts as a level of its own.
    const QStringList areas = d.areaNames(session_->levels());
    const auto entranceAt = [&](int r) {
        const QString area = areaAt(r);
        return areas.contains(area) ? d.area(session_->levels(), area).header.value(1) : -1;
    };
    const int first = row - at.second;
    int level = 1;
    for (int r = first; r < row; ++r) {
        const int entrance = entranceAt(r);
        if (entrance != 6 && entrance != 7) ++level;
    }
    const int entrance = entranceAt(row);
    const QString number = QStringLiteral("%1-%2").arg(at.first).arg(level);
    return entrance == 6 || entrance == 7 ? number + tr(" · pipe intro") : number;
}

QVector<sdk::TableColumn> SmbRoutesModel::columns() const {
    sdk::TableColumn course;
    course.key = QStringLiteral("course");
    course.label = tr("Course");
    course.width = 170;
    course.monospace = true;
    sdk::TableColumn step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.width = 90;
    step.muted = true;
    step.monospace = true;
    sdk::TableColumn area;
    area.key = QStringLiteral("area");
    area.label = tr("Area");
    area.editable = true;
    sdk::TableColumn alternate;
    alternate.key = QStringLiteral("alternate");
    alternate.label = tr("Alt bit");
    alternate.width = 100;
    alternate.monospace = true;
    alternate.editable = true;
    sdk::TableColumn byte;
    byte.key = QStringLiteral("byte");
    byte.label = tr("Byte");
    byte.width = 90;
    byte.muted = true;
    byte.monospace = true;
    return {course, step, area, alternate, byte};
}

int SmbRoutesModel::rowCount() const {
    int rows = 0;
    const SmbResourceDocument& d = session_->document();
    for (int w = 1; w <= d.worldCount(session_->world()); ++w) {
        rows += d.routes(session_->world(), w).size();
    }
    return rows;
}

QString SmbRoutesModel::cell(int row, int column) const {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return {};
    }
    const SmbRoute route = session_->document().routes(session_->world(), at.first).at(at.second);
    switch (column) {
    case 0: return courseLabel(row);
    case 1: return QStringLiteral("%1 / %2").arg(at.first).arg(at.second + 1);
    case 2: {
        const QString area = areaAt(row);
        return session_->document().areaNames(session_->levels()).contains(area)
                   ? areaLabel(area)
                   : areaLabel(area) + tr(" (missing)");
    }
    case 3: return route.alternate ? QStringLiteral("set") : QStringLiteral("—");
    case 4: return byteHex((route.alternate ? 0x80 : 0) | (route.areaType << 5) | route.areaIndex);
    default: return {};
    }
}

bool SmbRoutesModel::isRowDirty(int row) const {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return false;
    }
    const SmbRoute now = session_->document().routes(session_->world(), at.first).value(at.second);
    const SmbRoute saved = session_->saved().routes(session_->world(), at.first).value(at.second);
    return now.areaType != saved.areaType || now.areaIndex != saved.areaIndex || now.alternate != saved.alternate;
}

void SmbRoutesModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        const QString area = areaAt(row);
        if (session_->document().areaNames(session_->levels()).contains(area)) {
            session_->selectStart(area);
        }
        emit selectionChanged();
    }
}

bool SmbRoutesModel::setArea(int row, const QString& area, QString* error) {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return false;
    }
    const QString name = areaName(area);
    if (!session_->document().areaNames(session_->levels()).contains(name)) {
        if (error) {
            *error = tr("There is no area \"%1\". Areas are named like Ground 6 or Castle 2.").arg(area);
        }
        return false;
    }
    SmbRoute route = session_->document().routes(session_->world(), at.first).at(at.second);
    route.areaType = session_->document().areaTypes(session_->levels()).indexOf(areaTypeOf(name));
    route.areaIndex = name.section(QLatin1Char('_'), -1).toInt() - 1;
    const QString world = session_->world();
    const int number = at.first;
    const int step = at.second;
    const bool ok = session_->edit(tr("Route %1 step %2 to %3").arg(number).arg(step + 1).arg(areaLabel(name)),
                                   [world, number, step, route](SmbResourceDocument& d) { d.setRoute(world, number, step, route); },
                                   error);
    if (ok && row == row_) {
        session_->selectStart(name);
    }
    return ok;
}

bool SmbRoutesModel::setAlternate(int row, bool alternate, QString* error) {
    const QPair<int, int> at = stepAt(row);
    if (at.first <= 0) {
        return false;
    }
    SmbRoute route = session_->document().routes(session_->world(), at.first).at(at.second);
    route.alternate = alternate;
    const QString world = session_->world();
    const int number = at.first;
    const int step = at.second;
    return session_->edit(tr("Route %1 step %2 alternate bit").arg(number).arg(step + 1),
                          [world, number, step, route](SmbResourceDocument& d) { d.setRoute(world, number, step, route); },
                          error);
}

QStringList SmbRoutesModel::cellChoices(int, int column) const {
    if (column == 2) {
        QStringList labels;
        for (const QString& name : session_->document().areaNames(session_->levels())) {
            labels << areaLabel(name);
        }
        return labels;
    }
    if (column == 3) {
        return {QStringLiteral("—"), QStringLiteral("set")};
    }
    return {};
}

QVector<QImage> SmbRoutesModel::cellChoiceIcons(int, int column) const {
    QVector<QImage> icons;
    if (column != 2) {
        return icons;
    }
    // The first screen of each area, small.
    for (const QString& name : session_->document().areaNames(session_->levels())) {
        const QImage preview = session_->areaPreview(name, 8);
        icons << preview.copy(0, preview.height() - 128, 128, 128);
    }
    return icons;
}

bool SmbRoutesModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column == 2) {
        return setArea(row, value, error);
    }
    if (column == 3) {
        return setAlternate(row, value == QLatin1String("set"), error);
    }
    return TableModel::setCell(row, column, value, error);
}

bool SmbRoutesModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("area")) {
        return setArea(row_, value.toString(), error);
    }
    if (key == QLatin1String("alternate")) {
        return setAlternate(row_, value.toString() == QLatin1String("Set"), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Inspection SmbRoutesModel::inspect() const {
    sdk::Inspection inspection;
    const QPair<int, int> at = stepAt(row_);
    if (at.first <= 0) {
        return inspection;
    }
    const SmbRoute route = session_->document().routes(session_->world(), at.first).at(at.second);
    inspection.kicker = tr("Route step");
    inspection.title = tr("World %1 · %2").arg(courseLabel(row_)).arg(areaLabel(areaAt(row_)));
    sdk::Field area;
    area.key = QStringLiteral("area");
    area.label = tr("Area");
    area.type = sdk::Field::Type::Choice;
    for (const QString& name : session_->document().areaNames(session_->levels())) {
        area.choices << areaLabel(name);
    }
    area.value = areaLabel(areaAt(row_));
    sdk::Field alternate;
    alternate.key = QStringLiteral("alternate");
    alternate.label = tr("Alternate bit");
    alternate.type = sdk::Field::Type::Choice;
    alternate.choices = QStringList{tr("Clear"), QStringLiteral("Set")};
    alternate.value = route.alternate ? QStringLiteral("Set") : tr("Clear");
    sdk::Field step;
    step.key = QStringLiteral("step");
    step.label = tr("Step");
    step.value = tr("%1 of %2").arg(at.second + 1).arg(session_->document().routes(session_->world(), at.first).size());
    step.monospace = true;
    inspection.fields = {area, alternate, step};
    inspection.note = tr("Each world plays its steps in order; a pipe intro (an area whose entrance "
                         "walks the player into a pipe) shares the level number of the next step. The "
                         "steps per world are fixed. F4 plays the selected step's area.");
    inspection.engineTag = QStringLiteral("smb.world.v1");
    inspection.engine = {{tr("Route byte"), cell(row_, 4)},
                         {tr("Area type"), QString::number(route.areaType)},
                         {tr("Area index"), QString::number(route.areaIndex)}};
    return inspection;
}

QString SmbRoutesModel::statusText() const {
    return tr("World %1 · %2").arg(courseLabel(row_), areaLabel(areaAt(row_)));
}

QString SmbRoutesModel::startPoint() const {
    return session_->selectedStart();
}

// ---------------------------------------------------------------- physics

SmbPhysicsModel::SmbPhysicsModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SmbPhysicsModel::columns() const {
    QVector<sdk::TableColumn> result;
    sdk::TableColumn name;
    name.key = QStringLiteral("name");
    name.label = tr("Table");
    name.width = 200;
    name.muted = true;
    result << name;
    for (int i = 0; i < kPhysicsColumns; ++i) {
        sdk::TableColumn value;
        value.key = QStringLiteral("v%1").arg(i);
        value.label = QString::number(i);
        value.width = 74;
        value.monospace = true;
        value.editable = true;
        result << value;
    }
    sdk::TableColumn meaning;
    meaning.key = QStringLiteral("index");
    meaning.label = tr("Indexed by");
    meaning.muted = true;
    result << meaning;
    return result;
}

int SmbPhysicsModel::rowCount() const {
    return session_->document().physicsTables(session_->world()).size();
}

QString SmbPhysicsModel::cell(int row, int column) const {
    const QVector<SmbPhysicsTable> tables = session_->document().physicsTables(session_->world());
    if (row < 0 || row >= tables.size()) {
        return {};
    }
    const SmbPhysicsTable& table = tables.at(row);
    if (column == 0) {
        return physicsLabel(table.name);
    }
    if (column == kPhysicsColumns + 1) {
        return physicsInfo(table.name).entries.mid(0, table.values.size()).join(QStringLiteral(" · "));
    }
    const int index = column - 1;
    return index < table.values.size() ? QString::number(table.values.at(index)) : QString();
}

bool SmbPhysicsModel::isRowDirty(int row) const {
    const QVector<SmbPhysicsTable> now = session_->document().physicsTables(session_->world());
    const QVector<SmbPhysicsTable> saved = session_->saved().physicsTables(session_->world());
    return row >= 0 && row < now.size() && row < saved.size() && now.at(row).values != saved.at(row).values;
}

void SmbPhysicsModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SmbPhysicsModel::setValue(int row, int index, int value, QString* error) {
    const QVector<SmbPhysicsTable> tables = session_->document().physicsTables(session_->world());
    if (row < 0 || row >= tables.size() || index < 0 || index >= tables.at(row).values.size()) {
        if (error) *error = tr("This table has no entry %1.").arg(index);
        return false;
    }
    const QString world = session_->world();
    return session_->edit(QStringLiteral("%1 (%2)").arg(physicsLabel(tables.at(row).name), entryLabel(tables.at(row).name, index)),
                          [world, row, index, value](SmbResourceDocument& d) { d.setPhysicsValue(world, row, index, value); },
                          error);
}

bool SmbPhysicsModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column < 1 || column > kPhysicsColumns) {
        return TableModel::setCell(row, column, value, error);
    }
    bool ok = false;
    const QString text = value.trimmed();
    const int number = text.startsWith(QLatin1Char('$')) ? text.mid(1).toInt(&ok, 16) : text.toInt(&ok);
    if (!ok) {
        if (error) *error = tr("Enter a number (decimal, or hex with $).");
        return false;
    }
    return setValue(row, column - 1, number, error);
}

bool SmbPhysicsModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key.startsWith(QLatin1Char('v'))) {
        return setValue(row_, key.mid(1).toInt(), value.toInt(), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Inspection SmbPhysicsModel::inspect() const {
    sdk::Inspection inspection;
    const QVector<SmbPhysicsTable> tables = session_->document().physicsTables(session_->world());
    if (row_ < 0 || row_ >= tables.size()) {
        return inspection;
    }
    const SmbPhysicsTable& table = tables.at(row_);
    inspection.kicker = tr("Physics table");
    inspection.title = physicsLabel(table.name);
    for (int i = 0; i < table.values.size(); ++i) {
        sdk::Field f;
        f.key = QStringLiteral("v%1").arg(i);
        f.label = entryLabel(table.name, i);
        f.type = sdk::Field::Type::Integer;
        f.value = table.values.at(i);
        f.minimum = table.isSigned ? -128 : 0;
        f.maximum = table.isSigned ? 127 : 255;
        f.hint = byteHex(table.values.at(i));
        f.monospace = true;
        inspection.fields << f;
    }
    const PhysicsInfo& info = physicsInfo(table.name);
    inspection.note = QString::fromLatin1(info.meaning);
    if (info.entries.size() == 7) {
        inspection.note += tr(" The entry is picked when the jump starts: by running speed, or "
                              "swimming (and in a whirlpool).");
    }
    inspection.engineTag = QStringLiteral("smb.world.v1");
    inspection.engine = {{tr("Table"), table.name},
                         {tr("Bytes"), QStringLiteral("%1, %2").arg(table.values.size())
                                                       .arg(table.isSigned ? QStringLiteral("signed") : QStringLiteral("unsigned"))}};
    return inspection;
}

QString SmbPhysicsModel::statusText() const {
    const QVector<SmbPhysicsTable> tables = session_->document().physicsTables(session_->world());
    return row_ >= 0 && row_ < tables.size() ? physicsLabel(tables.at(row_).name) : QString();
}

}  // namespace rl::smb
