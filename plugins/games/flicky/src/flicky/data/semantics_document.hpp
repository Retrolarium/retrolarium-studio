#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace rl::flicky {

class FlickySemanticsDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Source-owned semantics.json: 52 fixed-capacity ASCII strings and 28
// Mega Drive CRAM palettes. Unrecognized JSON fields are preserved.
class FlickySemanticsDocument final {
public:
    static FlickySemanticsDocument load(const QString &path);
    static FlickySemanticsDocument fromJson(const QJsonObject &root);

    [[nodiscard]] QJsonObject toJson() const;
    void save(const QString &path) const;

    [[nodiscard]] QStringList textIds() const;
    [[nodiscard]] QString textValue(const QString &id) const;
    [[nodiscard]] QString baselineTextValue(const QString &id) const;
    [[nodiscard]] int textCapacity(const QString &id) const;
    void validateTextValue(const QString &id, const QString &value) const;
    void setTextValue(const QString &id, const QString &value);
    void restoreText(const QString &id);

    [[nodiscard]] QStringList paletteIds() const;
    [[nodiscard]] QVector<int> paletteColours(const QString &id) const;
    [[nodiscard]] QVector<int> baselinePaletteColours(const QString &id) const;
    [[nodiscard]] int colour(const QString &id, int index) const;
    void setColour(const QString &id, int index, int value);
    void restorePalette(const QString &id);

private:
    static int recordIndex(const QJsonObject &root, const char *section,
                           const QString &id);
    static QVector<int> colours(const QJsonObject &root, const QString &id);

    QJsonObject root_;
    QJsonObject baseline_;
};

} // namespace rl::flicky
