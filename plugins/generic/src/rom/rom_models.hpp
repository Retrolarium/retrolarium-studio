#pragma once

#include "rom/tile_formats.hpp"
#include "sdk/editor_model.hpp"

#include <QByteArray>

namespace rl::rom {

class RomSession;

// The ROM's bytes for the hex editor: overwritten in place, one undo step a
// change, and where each byte sits in a NES cartridge.
class HexModel final : public sdk::HexModel {
    Q_OBJECT

public:
    explicit HexModel(RomSession* session);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    int size() const override;
    QByteArray bytes(int offset, int length) const override;
    bool isByteDirty(int offset) const override;
    bool writeBytes(int offset, const QByteArray& data, const QString& text, QString* error) override;

private:
    // "PRG bank 1, $8123" or "CHR ROM $0123" on a NES; empty elsewhere.
    QString place(int offset) const;

    RomSession* session_;
};

// Raw 8x8 patterns of one Tiles document (the whole file, or one tile group
// the session found) in a layout chosen from tiles::formats(): the system's
// own or the group's to begin with. Graphics the game packs or compresses do
// not show here.
class RomTileModel final : public sdk::TileModel {
    Q_OBJECT

public:
    explicit RomTileModel(RomSession* session, const QString& document = QStringLiteral("all"));

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

    int tileCount() const override;
    int penCount() const override { return 1 << format().bits; }
    bool canShiftStart() const override { return true; }
    bool shiftStart(int bytes) override;
    QVector<QColor> penColors() const override;
    QVector<quint8> tilePixels(int tile) const override;
    bool isTileDirty(int tile) const override;
    int currentTile() const override { return tile_; }
    void setCurrentTile(int tile) override;
    int currentPen() const override { return pen_; }
    void setCurrentPen(int pen) override;

    void beginStroke() override;
    bool setPixel(QPoint pixel, int pen) override;
    void endStroke() override;
    bool fill(QPoint pixel, int pen, QString* error) override;

    // The layout shown (a tiles::formats() id).
    QString formatId() const { return format_; }
    void setFormat(const QString& id);

private:
    const tiles::Format& format() const;
    int tileOffset(int tile) const;
    QString formatLabel(const QString& id) const;
    QStringList colorChoices() const;
    void writePixels(QByteArray* bytes, int tile, const QVector<quint8>& pixels) const;

    RomSession* session_;
    int offset_ = 0;  // the document's first byte
    int length_ = 0;
    QString format_;
    int tile_ = 0;
    int pen_ = 3;
    int colors_ = 0;
    bool stroking_ = false;
    QByteArray before_;
};

// The header, field by field: iNES (PRG and CHR sizes, mapper, mirroring)
// or the Mega Drive's (titles, serial, checksum, region).
class HeaderModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit HeaderModel(RomSession* session);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool isReadOnly() const override { return true; }
    QString readOnlyReason() const override;
    QVector<sdk::EditorAction> actions() const override;
    void triggerAction(const QString& id) override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override { row_ = row; }

private:
    QVector<QPair<QString, QString>> rows() const;
    // The Mega Drive checksum: the words from $200 on, added up.
    quint16 megaDriveChecksum() const;

    RomSession* session_;
    int row_ = 0;
};

// The ROM as it is now in MAME (unsaved edits too).
class RomPlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit RomPlaytestModel(RomSession* session);

    QString title() const override { return tr("Run the ROM"); }
    sdk::Inspection inspect() const override;
    QSize screenSize() const override;
    QString coreLabel() const override;
    QString startLabel() const override { return QString(); }
    void play() override;

private:
    RomSession* session_;
};

}  // namespace rl::rom
