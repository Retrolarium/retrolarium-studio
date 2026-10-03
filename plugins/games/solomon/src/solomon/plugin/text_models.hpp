#pragma once

#include "sdk/editor_model.hpp"
#include "solomon/data/text.hpp"

namespace rl::solomon {

class SolomonSession;

// A screen the game draws from text runs: which runs, in which font bank
// and palette.
struct TextScreen {
    QString id;
    QString label;
    QStringList strings;    // TextString ids
    int bank = 3;           // CHR bank whose background table holds the font
    int paletteProgram = 5; // static palette program the screen loads
    bool titleBackdrop = false;
};

const QVector<TextScreen>& textScreens();
// The screen's name table as the game draws it (256x240), from `draft`
// when it is set; the title text shows over the dimmed title layers.
QImage renderTextScreen(const SolomonSession* session, const TextScreen& screen, int bank,
                        const SolomonText* draft = nullptr);

// Every run of text as a table: where it shows, what it says.
class SolomonTextTableModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit SolomonTextTableModel(SolomonSession* session);

    QString title() const override { return QStringLiteral("Text"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    int maxLength(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    SolomonSession* session_;
    int row_ = 0;
};

// One text screen on its 32x30 name table: the runs are objects to select
// and drag, their cells can be painted with font tiles, and the inspector
// edits the selected run's text.
class SolomonTextScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SolomonTextScreenModel(SolomonSession* session, const TextScreen& screen);

    QString title() const override { return screen_.label; }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;
    sdk::Library sheet() const override;

    QSize gridSize() const override { return {32, 30}; }
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    SolomonText text() const;
    // The run and character each cell shows.
    QPair<QString, int> writerAt(QPoint cell) const;
    bool commit(const SolomonText& next, const QString& label, QString* error);

    SolomonSession* session_;
    TextScreen screen_;
    int bank_;
    int brush_ = 0x0A;
    QString selected_;
    bool drafting_ = false;
    SolomonText draft_;
    QPoint dragOrigin_;
};

// A static palette program (01 room intro, 05 title and ending, 13
// licence): four background rows the screen loads.
class SolomonStaticPaletteModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    SolomonStaticPaletteModel(SolomonSession* session, int program);

    static QString programLabel(int program);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Gamut; }
    QString pickerTitle() const override { return QStringLiteral("NES palette · 64 colours"); }
    QVector<QColor> gamut() const override;
    QStringList gamutLabels() const override;
    int currentGamutIndex() const override;
    bool assignGamut(int index, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    SolomonSession* session_;
    int program_;
    QPoint entry_;
};

}  // namespace rl::solomon
