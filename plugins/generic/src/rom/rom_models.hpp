#pragma once

#include "rom/tile_formats.hpp"
#include "sdk/editor_model.hpp"

#include <QByteArray>

namespace rl::rom {

class RomSession;

// The bytes, sixteen to a row: an offset, the bytes in hexadecimal (each
// one edited by typing 00-FF) and the text they spell.
class HexModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit HexModel(RomSession* session);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    RomSession* session_;
    int row_ = 0;
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
