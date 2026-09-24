#pragma once

#include <retrolarium/flicky_collision.hpp>

#include <QJsonObject>
#include <QHash>
#include <QPoint>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <stdexcept>
#include <vector>

namespace retrolarium {

class FlickyLevelDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Native editing model for the source project's level/levels.json contract.
// Unknown fields are preserved by value. Collision and placement data follow
// independent round/special aliases; source-owned collision bytes stay exact
// until a layout's cells actually change.
class FlickyLevelDocument final {
public:
    static FlickyLevelDocument load(const QString &path);
    static FlickyLevelDocument fromJson(const QJsonObject &root);

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] QPair<int, int> capacityUsage() const;
    [[nodiscard]] QPair<int, int> capacityLimits() const;
    void validateCapacity() const;
    void save(const QString &path) const;

    [[nodiscard]] int layoutForRound(int round) const;
    [[nodiscard]] int objectLayoutForRound(int round, const QString &field) const;
    [[nodiscard]] QVector<int> roundsForLayout(int layout) const;
    [[nodiscard]] QVector<int> roundsForObjectLayout(int layout, const QString &field) const;
    [[nodiscard]] static QStringList objectFields();
    [[nodiscard]] static bool isSpecialObjectField(const QString &field);
    [[nodiscard]] static bool isVariableObjectField(const QString &field);
    [[nodiscard]] QVector<QPoint> objectPositions(int layout, const QString &field) const;
    [[nodiscard]] QVector<QPoint> baselineObjectPositions(int layout, const QString &field) const;
    void setObjectPosition(int layout, const QString &field, int index, QPoint position);
    void addObjectPosition(int layout, const QString &field, QPoint position);
    void removeObjectPosition(int layout, const QString &field, int index);
    void replaceObjectGroup(int layout, const QString &field,
                            const QVector<QPoint> &positions);
    void restoreObjectGroup(int layout, const QString &field);
    [[nodiscard]] int cell(int layout, int x, int y) const;
    void setCell(int layout, int x, int y, int value);
    void restoreLayout(int layout);
    [[nodiscard]] std::vector<std::uint8_t> encodedCollision(int layout) const;
    [[nodiscard]] std::vector<std::uint8_t> encodedLevel(int layout) const;
    [[nodiscard]] std::vector<std::uint8_t> encodedSpecial(int layout) const;
    [[nodiscard]] const FlickyCollisionGrid &grid(int layout) const;
    [[nodiscard]] const FlickyCollisionGrid &baselineGrid(int layout) const;

private:
    static void requireLayout(int layout);
    static void requireCell(int x, int y);

    QJsonObject root_;
    std::array<int, 48> roundLayouts_ {};
    std::array<int, 48> roundSpecialLayouts_ {};
    std::array<QHash<QString, QVector<QPoint>>, 36> objects_ {};
    std::array<QHash<QString, QVector<QPoint>>, 36> baselineObjects_ {};
    std::array<FlickyCollisionGrid, 36> grids_ {};
    std::array<FlickyCollisionGrid, 36> baselines_ {};
    std::array<std::vector<std::uint8_t>, 36> baselineStreams_ {};
};

} // namespace retrolarium
