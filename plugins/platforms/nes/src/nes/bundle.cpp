#include "nes/bundle.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace rl::nes {

namespace {

[[noreturn]] void fail(const QString& message) {
    throw BundleError(message.toStdString());
}

}  // namespace

QVector<int> intsOf(const QJsonValue& value) {
    QVector<int> result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue& item : array) {
        result << item.toInt();
    }
    return result;
}

Bundle Bundle::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
    }
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &error);
    if (json.isNull() || !json.isObject()) {
        fail(QStringLiteral("%1 is not a JSON document: %2").arg(path, error.errorString()));
    }
    return fromJson(json.object());
}

Bundle Bundle::fromJson(const QJsonObject& root) {
    if (!root.value(QStringLiteral("resources")).isArray()) {
        fail(QStringLiteral("The document has no resources."));
    }
    Bundle bundle;
    bundle.root_ = root;
    return bundle;
}

void Bundle::save(const QString& path) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
    file.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        fail(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    }
}

QString Bundle::game() const {
    return root_.value(QStringLiteral("game")).toString();
}

QString Bundle::revision() const {
    return root_.value(QStringLiteral("revision")).toString();
}

int Bundle::indexOf(const QString& id) const {
    const QJsonArray resources = root_.value(QStringLiteral("resources")).toArray();
    for (int i = 0; i < resources.size(); ++i) {
        if (resources.at(i).toObject().value(QStringLiteral("id")).toString() == id) {
            return i;
        }
    }
    return -1;
}

QStringList Bundle::resourceIds(const QString& codec) const {
    QStringList ids;
    for (const QJsonValue& value : root_.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        if (resource.value(QStringLiteral("codec")).toString() == codec) {
            ids << resource.value(QStringLiteral("id")).toString();
        }
    }
    return ids;
}

QJsonObject Bundle::resource(const QString& id) const {
    const int index = indexOf(id);
    if (index < 0) {
        fail(QStringLiteral("The project has no resource %1.").arg(id));
    }
    return root_.value(QStringLiteral("resources")).toArray().at(index).toObject();
}

void Bundle::setResource(const QString& id, const QJsonObject& resource) {
    const int index = indexOf(id);
    if (index < 0) {
        fail(QStringLiteral("The project has no resource %1.").arg(id));
    }
    QJsonArray resources = root_.value(QStringLiteral("resources")).toArray();
    resources.replace(index, resource);
    root_.insert(QStringLiteral("resources"), resources);
}

int Bundle::tileCount(const QString& id) const {
    return resource(id).value(QStringLiteral("tiles")).toArray().size();
}

QVector<quint8> Bundle::tilePixels(const QString& id, int tile) const {
    const QJsonArray tiles = resource(id).value(QStringLiteral("tiles")).toArray();
    if (tile < 0 || tile >= tiles.size()) {
        fail(QStringLiteral("%1 has no tile %2.").arg(id).arg(tile));
    }
    QVector<quint8> pixels;
    pixels.reserve(64);
    for (const QJsonValue& row : tiles.at(tile).toArray()) {
        for (const QJsonValue& pen : row.toArray()) {
            pixels << quint8(pen.toInt() & 3);
        }
    }
    pixels.resize(64);
    return pixels;
}

QVector<quint8> Bundle::allPixels(const QString& id) const {
    const QJsonArray tiles = resource(id).value(QStringLiteral("tiles")).toArray();
    QVector<quint8> pixels(tiles.size() * 64, 0);
    int at = 0;
    for (const QJsonValue& tile : tiles) {
        int i = 0;
        for (const QJsonValue& row : tile.toArray()) {
            for (const QJsonValue& pen : row.toArray()) {
                if (i < 64) pixels[at + i] = quint8(pen.toInt() & 3);
                ++i;
            }
        }
        at += 64;
    }
    return pixels;
}

void Bundle::setTilePixels(const QString& id, int tile, const QVector<quint8>& pixels) {
    QJsonObject r = resource(id);
    QJsonArray tiles = r.value(QStringLiteral("tiles")).toArray();
    if (tile < 0 || tile >= tiles.size() || pixels.size() != 64) {
        fail(QStringLiteral("%1 has no tile %2.").arg(id).arg(tile));
    }
    QJsonArray rows;
    for (int y = 0; y < 8; ++y) {
        QJsonArray row;
        for (int x = 0; x < 8; ++x) {
            row.append(int(pixels.at(y * 8 + x) & 3));
        }
        rows.append(row);
    }
    tiles.replace(tile, rows);
    r.insert(QStringLiteral("tiles"), tiles);
    setResource(id, r);
}

QVector<int> Bundle::tableValues(const QString& id) const {
    return intsOf(resource(id).value(QStringLiteral("values")));
}

void Bundle::setTableValue(const QString& id, int index, int value) {
    QJsonObject r = resource(id);
    QJsonArray values = r.value(QStringLiteral("values")).toArray();
    if (index < 0 || index >= values.size()) {
        fail(QStringLiteral("%1 has no entry %2.").arg(id).arg(index));
    }
    if (value < 0 || value > 63) {
        fail(QStringLiteral("An NES colour is $00-$3F."));
    }
    values.replace(index, value);
    r.insert(QStringLiteral("values"), values);
    setResource(id, r);
}

QString Bundle::paletteModel(const QString& id) const {
    return resource(id).value(QStringLiteral("palette_model")).toString(QStringLiteral("nes"));
}

}  // namespace rl::nes
