#pragma once

#include <retrolarium/flicky_collision.hpp>

#include <QJsonObject>
#include <QString>
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
// Unknown and non-collision fields are preserved by value. The source-owned
// baseline stream is retained verbatim until a layout's cells actually change.
class FlickyLevelDocument final {
public:
    static FlickyLevelDocument load(const QString &path);
    static FlickyLevelDocument fromJson(const QJsonObject &root);

    [[nodiscard]] QJsonObject toJson() const;
    void save(const QString &path) const;

    [[nodiscard]] int layoutForRound(int round) const;
    [[nodiscard]] QVector<int> roundsForLayout(int layout) const;
    [[nodiscard]] int cell(int layout, int x, int y) const;
    void setCell(int layout, int x, int y, int value);
    void restoreLayout(int layout);
    [[nodiscard]] std::vector<std::uint8_t> encodedCollision(int layout) const;
    [[nodiscard]] const FlickyCollisionGrid &grid(int layout) const;
    [[nodiscard]] const FlickyCollisionGrid &baselineGrid(int layout) const;

private:
    static void requireLayout(int layout);
    static void requireCell(int x, int y);

    QJsonObject root_;
    std::array<int, 48> roundLayouts_ {};
    std::array<FlickyCollisionGrid, 36> grids_ {};
    std::array<FlickyCollisionGrid, 36> baselines_ {};
    std::array<std::vector<std::uint8_t>, 36> baselineStreams_ {};
};

} // namespace retrolarium
