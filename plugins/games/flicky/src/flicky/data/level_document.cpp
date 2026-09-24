#include <retrolarium/flicky_level_document.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace retrolarium {
namespace {

[[noreturn]] void fail(const QString &message)
{
    throw FlickyLevelDocumentError(message.toStdString());
}

int integer(const QJsonValue &value, int minimum, int maximum, const QString &field)
{
    if (!value.isDouble()) fail(field + QStringLiteral(" must be an integer"));
    const double number = value.toDouble();
    const int result = value.toInt(-1);
    if (number != result || result < minimum || result > maximum) {
        fail(field + QStringLiteral(" is outside its valid range"));
    }
    return result;
}

FlickyCollisionGrid gridFromJson(const QJsonValue &value, int layout)
{
    const auto rows = value.toArray();
    if (rows.size() != flickyMapHeight) {
        fail(QStringLiteral("layout %1 must have 28 collision rows").arg(layout));
    }
    FlickyCollisionGrid grid {};
    for (int y = 0; y < flickyMapHeight; ++y) {
        const auto row = rows.at(y).toArray();
        if (row.size() != flickyMapWidth) {
            fail(QStringLiteral("layout %1 row %2 must have 32 cells")
                     .arg(layout).arg(y));
        }
        for (int x = 0; x < flickyMapWidth; ++x) {
            grid[static_cast<std::size_t>(y * flickyMapWidth + x)] =
                static_cast<std::uint8_t>(integer(row.at(x), 0, 1,
                    QStringLiteral("collision cell")));
        }
    }
    return grid;
}

QJsonArray gridToJson(const FlickyCollisionGrid &grid)
{
    QJsonArray rows;
    for (int y = 0; y < flickyMapHeight; ++y) {
        QJsonArray row;
        for (int x = 0; x < flickyMapWidth; ++x) {
            row.append(static_cast<int>(
                grid[static_cast<std::size_t>(y * flickyMapWidth + x)]));
        }
        rows.append(row);
    }
    return rows;
}

std::vector<std::uint8_t> byteStream(const QJsonValue &value, int layout)
{
    if (!value.isArray()) {
        fail(QStringLiteral("layout %1 has no baseline collision stream")
                 .arg(layout));
    }
    const auto array = value.toArray();
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(array.size()));
    for (const auto &item : array) {
        result.push_back(static_cast<std::uint8_t>(integer(item, 0, 255,
            QStringLiteral("baseline collision byte"))));
    }
    return result;
}

} // namespace

FlickyLevelDocument FlickyLevelDocument::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("cannot open Flicky level document: %1").arg(path));
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("invalid Flicky level JSON: %1")
                 .arg(parseError.errorString()));
    }
    return fromJson(document.object());
}

FlickyLevelDocument FlickyLevelDocument::fromJson(const QJsonObject &root)
{
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || root.value(QStringLiteral("profile")).toString()
               != QStringLiteral("canonical")) {
        fail(QStringLiteral("unsupported Flicky level schema or profile"));
    }
    const auto map = root.value(QStringLiteral("map")).toObject();
    if (map.value(QStringLiteral("width")).toInt(-1) != flickyMapWidth
        || map.value(QStringLiteral("height")).toInt(-1) != flickyMapHeight) {
        fail(QStringLiteral("Flicky level map must be 32x28"));
    }
    const auto roundLayouts = root.value(QStringLiteral("round_layouts")).toArray();
    const auto layouts = root.value(QStringLiteral("layouts")).toArray();
    if (roundLayouts.size() != 48 || layouts.size() != 36) {
        fail(QStringLiteral("Flicky levels need 48 rounds and 36 layouts"));
    }
    FlickyLevelDocument document;
    document.root_ = root;
    for (int round = 0; round < 48; ++round) {
        document.roundLayouts_[static_cast<std::size_t>(round)] = integer(
            roundLayouts.at(round), 0, 35, QStringLiteral("round layout"));
    }
    for (int layout = 0; layout < 36; ++layout) {
        const auto record = layouts.at(layout).toObject();
        if (record.value(QStringLiteral("id")).toInt(-1) != layout) {
            fail(QStringLiteral("Flicky layout identifiers must be 0..35"));
        }
        document.grids_[static_cast<std::size_t>(layout)] = gridFromJson(
            record.value(QStringLiteral("collision")), layout);
        document.baselineStreams_[static_cast<std::size_t>(layout)] = byteStream(
            record.value(QStringLiteral("baseline_collision_stream")), layout);
        try {
            document.baselines_[static_cast<std::size_t>(layout)] =
                decodeFlickyCollision(
                    document.baselineStreams_[static_cast<std::size_t>(layout)]);
        } catch (const FlickyCollisionError &error) {
            fail(QStringLiteral("layout %1 baseline is invalid: %2")
                     .arg(layout).arg(QString::fromUtf8(error.what())));
        }
        if (document.grids_[static_cast<std::size_t>(layout)]
            != document.baselines_[static_cast<std::size_t>(layout)]) {
            (void)encodeFlickyCollision(
                document.grids_[static_cast<std::size_t>(layout)]);
        }
    }
    return document;
}

QJsonObject FlickyLevelDocument::toJson() const
{
    auto root = root_;
    auto layouts = root.value(QStringLiteral("layouts")).toArray();
    for (int index = 0; index < 36; ++index) {
        auto record = layouts.at(index).toObject();
        record.insert(QStringLiteral("collision"),
                      gridToJson(grids_[static_cast<std::size_t>(index)]));
        layouts.replace(index, record);
    }
    root.insert(QStringLiteral("layouts"), layouts);
    return root;
}

void FlickyLevelDocument::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("cannot save Flicky level document: %1").arg(path));
    }
    auto bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        fail(QStringLiteral("cannot commit Flicky level document: %1").arg(path));
    }
}

void FlickyLevelDocument::requireLayout(int layout)
{
    if (layout < 0 || layout >= 36) fail(QStringLiteral("invalid Flicky layout"));
}

void FlickyLevelDocument::requireCell(int x, int y)
{
    if (x < 0 || x >= flickyMapWidth || y < 0 || y >= flickyMapHeight) {
        fail(QStringLiteral("Flicky collision cell is outside 32x28"));
    }
}

int FlickyLevelDocument::layoutForRound(int round) const
{
    if (round < 0 || round >= 48) fail(QStringLiteral("invalid Flicky round"));
    return roundLayouts_[static_cast<std::size_t>(round)];
}

QVector<int> FlickyLevelDocument::roundsForLayout(int layout) const
{
    requireLayout(layout);
    QVector<int> rounds;
    for (int round = 0; round < 48; ++round) {
        if (layoutForRound(round) == layout) rounds.append(round);
    }
    return rounds;
}

int FlickyLevelDocument::cell(int layout, int x, int y) const
{
    requireLayout(layout);
    requireCell(x, y);
    return grids_[static_cast<std::size_t>(layout)]
                 [static_cast<std::size_t>(y * flickyMapWidth + x)];
}

void FlickyLevelDocument::setCell(int layout, int x, int y, int value)
{
    requireLayout(layout);
    requireCell(x, y);
    if (y < 2) fail(QStringLiteral("the first two collision rows are generated at runtime"));
    if (value != 0 && value != 1) fail(QStringLiteral("collision cells must be 0 or 1"));
    grids_[static_cast<std::size_t>(layout)]
          [static_cast<std::size_t>(y * flickyMapWidth + x)] =
              static_cast<std::uint8_t>(value);
}

void FlickyLevelDocument::restoreLayout(int layout)
{
    requireLayout(layout);
    grids_[static_cast<std::size_t>(layout)] =
        baselines_[static_cast<std::size_t>(layout)];
}

std::vector<std::uint8_t> FlickyLevelDocument::encodedCollision(int layout) const
{
    requireLayout(layout);
    const auto index = static_cast<std::size_t>(layout);
    if (grids_[index] == baselines_[index]) return baselineStreams_[index];
    return encodeFlickyCollision(grids_[index]);
}

const FlickyCollisionGrid &FlickyLevelDocument::grid(int layout) const
{
    requireLayout(layout);
    return grids_[static_cast<std::size_t>(layout)];
}

const FlickyCollisionGrid &FlickyLevelDocument::baselineGrid(int layout) const
{
    requireLayout(layout);
    return baselines_[static_cast<std::size_t>(layout)];
}

} // namespace retrolarium
