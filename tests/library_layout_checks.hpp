#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace rl::tests {

struct LibraryLayoutSnapshot {
    QJsonArray cards;
    QString error;
};

// Inspect real widget geometry, including cards below the scroll viewport.
// Do not activate layouts or process events here: that could hide a bad frame.
inline LibraryLayoutSnapshot inspectLibraryLayout(QWidget* root, const QStringList& expectedIds) {
    LibraryLayoutSnapshot result;
    QSet<QString> found;
    QVector<QPair<QString, QRect>> bounds;
    for (QWidget* card : root->findChildren<QWidget*>()) {
        const QString name = card->objectName();
        if (!name.startsWith(QLatin1String("libraryItem.")) || !card->isVisibleTo(root)) continue;
        const QRect rect(card->mapTo(root, QPoint()), card->size());
        result.cards.append(QJsonObject{{QStringLiteral("id"), name},
                                        {QStringLiteral("x"), rect.x()},
                                        {QStringLiteral("y"), rect.y()},
                                        {QStringLiteral("width"), rect.width()},
                                        {QStringLiteral("height"), rect.height()}});
        if (found.contains(name)) result.error = QStringLiteral("Duplicate library card: %1").arg(name);
        found.insert(name);
        if (rect.isEmpty() || card->height() < card->minimumHeight())
            result.error = QStringLiteral("Library card has invalid or compressed dimensions: %1").arg(name);
        if (!card->parentWidget()->rect().contains(card->geometry()))
            result.error = QStringLiteral("Library card extends outside its content container: %1").arg(name);
        bounds.append(qMakePair(name, rect));
    }
    QSet<QString> expected;
    for (const QString& id : expectedIds) expected.insert(QStringLiteral("libraryItem.%1").arg(id));
    if (found != expected)
        result.error = QStringLiteral("Library card set is incomplete or stale: expected %1, found %2")
                           .arg(expected.size())
                           .arg(found.size());
    for (int i = 0; i < bounds.size(); ++i) {
        for (int j = i + 1; j < bounds.size(); ++j) {
            if (bounds.at(i).second.intersects(bounds.at(j).second)) {
                result.error =
                    QStringLiteral("Library cards overlap: %1 and %2").arg(bounds.at(i).first, bounds.at(j).first);
                return result;
            }
        }
    }
    return result;
}

}  // namespace rl::tests
