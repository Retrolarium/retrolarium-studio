#include "alien_soldier/data/documents.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <stdexcept>

namespace rl::alien_soldier {

QString documentPath(Doc doc) {
    switch (doc) {
        case Doc::Levels:
            return QStringLiteral("content/levels/levels.json");
        case Doc::Maps:
            return QStringLiteral("content/levels/maps.json");
        case Doc::Demos:
            return QStringLiteral("content/levels/demos.json");
        case Doc::Tiles:
            return QStringLiteral("content/graphics/tiles.json");
        case Doc::Compressed:
            return QStringLiteral("content/graphics/compressed_tiles.json");
        case Doc::Palettes:
            return QStringLiteral("content/graphics/palettes.json");
        case Doc::Mappings:
            return QStringLiteral("content/graphics/mappings.json");
        case Doc::Animations:
            return QStringLiteral("content/graphics/animations.json");
        case Doc::Screens:
            return QStringLiteral("content/screens/screens.json");
        case Doc::Text:
            return QStringLiteral("content/text/text.json");
        case Doc::Sound:
            return QStringLiteral("content/sound/sound.json");
        case Doc::Envelopes:
            return QStringLiteral("content/sound/envelopes.json");
        case Doc::Pcm:
            return QStringLiteral("content/sound/pcm.json");
        case Doc::Stages:
            return QStringLiteral("content/levels/stages.json");
        case Doc::Objects:
            return QStringLiteral("content/graphics/objects.json");
        case Doc::Art:
            return QStringLiteral("content/graphics/art.json");
        case Doc::Bosses:
            return QStringLiteral("content/graphics/bosses.json");
    }
    return {};
}

bool isEditable(Doc doc) { return doc != Doc::Stages && doc != Doc::Objects && doc != Doc::Art; }

QJsonObject loadJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("cannot read %1").arg(path).toStdString());
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error(QStringLiteral("%1 is not a JSON object: %2")
                                     .arg(QFileInfo(path).fileName(), parse.errorString())
                                     .toStdString());
    }
    return document.object();
}

void saveJson(const QString& path, const QJsonObject& document) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error(QStringLiteral("cannot write %1").arg(path).toStdString());
    }
    file.write(QJsonDocument(document).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit()) {
        throw std::runtime_error(QStringLiteral("cannot write %1").arg(path).toStdString());
    }
}

Documents Documents::load(const QString& snapshot) {
    Documents documents;
    const QDir root(snapshot);
    for (int index = 0; index < kDocCount; ++index) {
        const Doc doc = static_cast<Doc>(index);
        Tracked& tracked = documents.tracked_[index];
        tracked.path = root.absoluteFilePath(documentPath(doc));
        // A derived document a project lacks (an older kit wrote it) is empty
        // until the kit reads the source again; so is the boss document,
        // which kits before it did not write (the kit adds it on a build).
        tracked.doc = (isEditable(doc) && doc != Doc::Bosses) || QFileInfo::exists(tracked.path)
                          ? loadJson(tracked.path)
                          : QJsonObject();
        tracked.saved = tracked.doc;
        // The kit keeps each document as first exported beside the snapshot
        // (../original/<path>); without it, Revert goes back to the opened one.
        const QString pristine = root.absoluteFilePath(QStringLiteral("../original/") + documentPath(doc));
        tracked.original = tracked.doc;
        if (isEditable(doc) && QFileInfo::exists(pristine)) {
            try {
                tracked.original = loadJson(pristine);
            } catch (const std::exception&) {
            }
        }
    }
    return documents;
}

int indexOfLabel(const QJsonArray& items, const QString& label) {
    for (int index = 0; index < items.size(); ++index) {
        if (items.at(index).toObject().value(QStringLiteral("label")).toString() == label) return index;
    }
    return -1;
}

QByteArray hexBytes(const QString& text) { return QByteArray::fromHex(text.toLatin1()); }

QString bytesHex(const QByteArray& bytes) { return QString::fromLatin1(bytes.toHex().toUpper()); }

}  // namespace rl::alien_soldier
