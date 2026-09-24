#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <stdexcept>

namespace retrolarium {

class FlickySequencesDocumentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Source-owned sequences.json: 224 Genesis sprite mappings and 50 animation
// sequences. Edits retain unknown JSON fields and preserve record cardinality.
class FlickySequencesDocument final {
public:
    static FlickySequencesDocument load(const QString &path);
    static FlickySequencesDocument fromJson(const QJsonObject &root);

    [[nodiscard]] QJsonObject toJson() const;
    void save(const QString &path) const;

    [[nodiscard]] QStringList mappingIds() const;
    [[nodiscard]] QJsonObject mapping(const QString &id) const;
    [[nodiscard]] QJsonObject baselineMapping(const QString &id) const;
    void replaceMapping(const QString &id, const QJsonObject &mapping);
    void setMappingRenderFlags(const QString &id, int value);
    void setMappingPiece(const QString &id, int index, const QJsonObject &piece);
    void restoreMapping(const QString &id);

    [[nodiscard]] QStringList animationIds() const;
    [[nodiscard]] QJsonObject animation(const QString &id) const;
    [[nodiscard]] QJsonObject baselineAnimation(const QString &id) const;
    [[nodiscard]] QStringList allowedFrames(const QString &kind) const;
    void replaceAnimation(const QString &id, const QJsonObject &animation);
    void setAnimationDelay(const QString &id, int value);
    void setAnimationFrame(const QString &id, int index, const QString &frame);
    void restoreAnimation(const QString &id);

private:
    static void validate(const QJsonObject &root);
    static int recordIndex(const QJsonObject &root, const char *section,
                           const QString &id);
    static QJsonObject record(const QJsonObject &root, const char *section,
                              const QString &id);
    void replaceRecord(const char *section, const QString &id,
                       const QJsonObject &replacement);

    QJsonObject root_;
    QJsonObject baseline_;
};

} // namespace retrolarium
