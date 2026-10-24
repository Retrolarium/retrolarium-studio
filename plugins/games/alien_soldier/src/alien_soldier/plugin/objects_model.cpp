#include "alien_soldier/plugin/objects_model.hpp"

#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

QString hex(int value, int digits) { return QStringLiteral("$%1").arg(value, digits, 16, QLatin1Char('0')).toUpper(); }

}  // namespace

ObjectsModel::ObjectsModel(AlienSoldierSession* session, const QString& block)
    : MappingCanvas(session), block_(block) {}

QPair<int, int> ObjectsModel::mapping() const {
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    const int index = indexOfLabel(blocks, block_);
    return index < 0 ? QPair<int, int>(-1, -1) : QPair<int, int>(index, frame_);
}

QVector<sdk::CompositionFrame> ObjectsModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QJsonArray blocks = session_->doc(Doc::Mappings).value(QStringLiteral("blocks")).toArray();
    const QJsonArray mappings =
        blocks.at(indexOfLabel(blocks, block_)).toObject().value(QStringLiteral("mappings")).toArray();
    for (int index = 0; index < mappings.size(); ++index) {
        sdk::CompositionFrame frame;
        frame.id = QString::number(index);
        frame.label = tr("Frame %1").arg(index + 1);
        frame.detail =
            tr("%n pieces", nullptr, mappings.at(index).toObject().value(QStringLiteral("pieces")).toArray().size());
        result.append(frame);
    }
    return result;
}

void ObjectsModel::setCurrentFrame(int frame) {
    frame_ = std::clamp(frame, 0, std::max(0, frames().size() - 1));
    clearPiece();
    emit selectionChanged();
    emit contentChanged();
}

QString ObjectsModel::statusText() const {
    return tr("%1 · frame %2 of %3").arg(block_).arg(frame_ + 1).arg(frames().size());
}

sdk::Inspection ObjectsModel::inspect() const {
    sdk::Inspection inspection;
    if (!selectedPiece().isEmpty()) {
        inspectPiece(inspection);
        return inspection;
    }
    inspection.kicker = tr("Object");
    inspection.title = block_;
    const QJsonObject objects = session_->doc(Doc::Objects);
    const QJsonObject record = objects.value(QStringLiteral("contexts")).toObject().value(block_).toObject();
    const auto row = [](const QString& label, const QString& value) {
        sdk::Field field;
        field.key = label;
        field.label = label;
        field.type = sdk::Field::Type::ReadOnly;
        field.value = value;
        field.monospace = true;
        return field;
    };
    if (record.isEmpty()) {
        inspection.note =
            tr("The game repository records nothing this object draws with yet, so its pieces show as "
               "outlines in the view's colours.");
    } else {
        const int attribute = record.value(QStringLiteral("attribute")).toInt();
        inspection.fields.append(row(tr("Attribute"), hex(attribute, 4)));
        inspection.fields.append(row(tr("Base tile"), hex(attribute & 0x7FF, 3)));
        inspection.fields.append(row(tr("Palette line"), QString::number((attribute >> 13) & 3)));
        for (const QJsonValue& value : record.value(QStringLiteral("loads")).toArray()) {
            const QJsonObject load = value.toObject();
            inspection.fields.append(row(tr("Art at %1").arg(hex(load.value(QStringLiteral("vram")).toInt(), 4)),
                                         load.value(QStringLiteral("source")).toString()));
        }
        QStringList colours;
        for (const QJsonValue& value : record.value(QStringLiteral("palettes")).toArray())
            colours << value.toArray().at(0).toString();
        if (!colours.isEmpty()) inspection.fields.append(row(tr("Colours"), colours.join(QStringLiteral(", "))));
        inspection.note = record.value(QStringLiteral("borrowed")).toBool()
                              ? tr("Not seen in play itself: it draws with what another block of its file was seen "
                                   "with. The tiles change with the art they come from.")
                              : tr("Drawn with the attribute, art and colours the game was seen using for it. The "
                                   "tiles change with the art they come from.");
    }
    QStringList scripts;
    for (const QJsonValue& value : objects.value(QStringLiteral("animations")).toObject().value(block_).toArray())
        scripts << value.toString();
    inspection.engineTag = QStringLiteral("alien-soldier.objects.v1");
    inspection.engine = {{tr("Animations"), scripts.isEmpty() ? tr("none") : scripts.join(QStringLiteral(", "))}};
    return inspection;
}

bool ObjectsModel::setField(const QString& key, const QVariant& value, QString* error) {
    return setPieceField(key, value, error);
}

}  // namespace rl::alien_soldier
