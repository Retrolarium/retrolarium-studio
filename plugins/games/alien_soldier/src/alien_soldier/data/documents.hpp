#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>

// Alien Soldier's documents, as the kit writes them into a project's source
// snapshot (content/...). Each is JSON; the session keeps them as values, so
// an edit is a whole-document snapshot.
namespace rl::alien_soldier {

enum class Doc {
    Levels,
    Maps,
    Demos,
    Tiles,
    Compressed,
    Palettes,
    Mappings,
    Animations,
    Screens,
    Text,
    Sound,
    Envelopes,
    Pcm,
    Stages,  // derived: what each stage loads, never edited
};

inline constexpr int kDocCount = 14;

// The document's path relative to the snapshot ("content/levels/levels.json").
QString documentPath(Doc doc);
bool isEditable(Doc doc);

// A document and its saved state; `revision` counts edits so that undoing
// back to the saved state makes it clean again.
struct Tracked {
    QJsonObject doc;
    QJsonObject saved;
    QJsonObject original;  // as the kit first exported it (Revert)
    QString path;
    quint64 revision = 0;
    quint64 savedRevision = 0;
    quint64 nextRevision = 1;

    bool isDirty() const { return revision != savedRevision; }
};

// Every document of a project, loaded from the snapshot folder.
class Documents {
public:
    // Throws std::runtime_error naming the document that cannot be read.
    static Documents load(const QString& snapshot);

    const QJsonObject& operator[](Doc doc) const { return tracked_[static_cast<int>(doc)].doc; }
    Tracked& tracked(Doc doc) { return tracked_[static_cast<int>(doc)]; }
    const Tracked& tracked(Doc doc) const { return tracked_[static_cast<int>(doc)]; }

private:
    std::array<Tracked, kDocCount> tracked_;
};

QJsonObject loadJson(const QString& path);
void saveJson(const QString& path, const QJsonObject& document);

// A list of objects in a document by their "label" key.
int indexOfLabel(const QJsonArray& items, const QString& label);

// 64 hexadecimal digits (or any whole number of bytes) as bytes.
QByteArray hexBytes(const QString& text);
QString bytesHex(const QByteArray& bytes);

}  // namespace rl::alien_soldier
