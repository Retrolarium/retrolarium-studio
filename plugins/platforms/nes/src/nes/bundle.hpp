#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace rl::nes {

class BundleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A kit document of typed NES resources ({"resources": [{id, codec, ...}]}),
// as `rltool game extract --resource resource-bundle` writes it. The JSON is
// kept whole, so keys this class does not know survive a save.
class Bundle {
public:
    static Bundle load(const QString& path);    // throws BundleError
    static Bundle fromJson(const QJsonObject& root);
    void save(const QString& path) const;       // throws BundleError

    const QJsonObject& root() const { return root_; }
    QString game() const;
    QString revision() const;

    QStringList resourceIds(const QString& codec) const;
    bool contains(const QString& id) const { return indexOf(id) >= 0; }
    QJsonObject resource(const QString& id) const;     // throws for an unknown id
    void setResource(const QString& id, const QJsonObject& resource);

    // nes.chr-2bpp.v1: tiles as 8 rows of 8 pens.
    int tileCount(const QString& id) const;
    QVector<quint8> tilePixels(const QString& id, int tile) const;     // 64, row-major
    // Every tile of a bank, 64 pens each, for drawing.
    QVector<quint8> allPixels(const QString& id) const;
    void setTilePixels(const QString& id, int tile, const QVector<quint8>& pixels);

    // nes.palette-table.v1: a flat list of master-palette indices.
    QVector<int> tableValues(const QString& id) const;
    void setTableValue(const QString& id, int index, int value);
    QString paletteModel(const QString& id) const;

    bool operator==(const Bundle& other) const { return root_ == other.root_; }
    bool operator!=(const Bundle& other) const { return root_ != other.root_; }

private:
    int indexOf(const QString& id) const;

    QJsonObject root_;
};

// QJsonValue arrays of integers.
QVector<int> intsOf(const QJsonValue& value);

}  // namespace rl::nes
