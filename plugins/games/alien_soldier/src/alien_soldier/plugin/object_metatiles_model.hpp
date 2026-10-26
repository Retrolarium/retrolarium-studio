#pragma once

#include "alien_soldier/data/sprites.hpp"
#include "sdk/editor_model.hpp"

#include <QStringList>

namespace rl::alien_soldier {

class AlienSoldierSession;

// The metatiles of one object file (an objects.json group): what its static
// pieces draw, each drawn with the art and colours of the blocks that use it
// (blocks drawing with different art keep theirs apart, a row each). A
// metatile changes in the inspector, or takes its first tile from the art's
// tile sheet, in every piece of those blocks at once; Objects and Animations
// put them into pieces from their own sheets.
class ObjectMetatilesModel final : public sdk::CompositionModel {
    Q_OBJECT

public:
    ObjectMetatilesModel(AlienSoldierSession* session, const QString& group);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QSize canvasSize() const override;
    QImage render() const override;
    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override { return selected_; }
    void selectPiece(const QString& id) override;

    sdk::Library sheet() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;

private:
    struct Entry {
        Metatile metatile;
        QStringList blocks;  // the blocks drawing with the same art
        int uses = 0;
        QPoint cell;  // in the sheet, in cells
    };
    QVector<Entry> entries() const;
    // The entry selected, else the first; -1 when there is none.
    int current(const QVector<Entry>& list) const;
    // Every piece of the entry's blocks drawing its metatile draws `to`.
    bool change(const Entry& entry, const Metatile& to, QString* error);

    AlienSoldierSession* session_;
    QString group_;
    QString selected_;
};

}  // namespace rl::alien_soldier
