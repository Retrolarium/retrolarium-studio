#pragma once

#include "editors/library_sheet.hpp"

#include <algorithm>

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

// Check the painted grid as well as the inspector's individual card widgets.
inline LibraryLayoutSnapshot inspectLibrarySheet(const editors::LibrarySheet* sheet, const sdk::Library& expected) {
    LibraryLayoutSnapshot result;
    QVector<QRect> bounds;
    QSet<QString> ids;
    if (sheet->itemCount() != expected.items.size()) {
        result.error = QStringLiteral("Library grid item count is stale");
        return result;
    }
    for (int i = 0; i < sheet->itemCount(); ++i) {
        const QString id = sheet->itemId(i);
        const QRect rect = sheet->itemRect(i);
        if (id != expected.items.at(i).id || ids.contains(id))
            result.error = QStringLiteral("Library grid item IDs are stale or duplicated");
        ids.insert(id);
        if (rect.isEmpty() || !sheet->rect().contains(rect))
            result.error = QStringLiteral("Library grid cell is compressed or outside its content widget");
        result.cards.append(QJsonObject{{QStringLiteral("id"), id},
                                        {QStringLiteral("x"), rect.x()},
                                        {QStringLiteral("y"), rect.y()},
                                        {QStringLiteral("width"), rect.width()},
                                        {QStringLiteral("height"), rect.height()}});
        bounds.append(rect);
    }
    std::sort(bounds.begin(), bounds.end(), [](const QRect& a, const QRect& b) { return a.top() < b.top(); });
    for (int i = 0; i < bounds.size(); ++i) {
        for (int j = i + 1; j < bounds.size() && bounds[j].top() <= bounds[i].bottom(); ++j) {
            if (bounds[i].intersects(bounds[j])) {
                result.error = QStringLiteral("Painted library grid cells overlap");
                return result;
            }
        }
    }
    return result;
}

}  // namespace rl::tests
