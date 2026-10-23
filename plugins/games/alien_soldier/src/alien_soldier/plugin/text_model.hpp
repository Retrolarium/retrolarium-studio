#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One text payload's records. The game stores glyph numbers, never letters:
// the text column shows them through the font's character list
// (config/names.json, catalog font_characters, kept by the game repository),
// a glyph without a character as [$2F]. An edited record keeps its number
// of glyphs (a shorter text is padded with spaces) and its fixed bytes
// (terminators, positions, counts); credits delays change in the inspector.
class TextModel final : public sdk::TableModel {
    Q_OBJECT

public:
    TextModel(AlienSoldierSession* session, const QString& label);

    static QString groupTitle(const QString& group);

    QString title() const override { return label_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    sdk::Budget cellBudget(int row, int column) const override;
    int maxLength(int row, int column) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    QJsonObject payload(const QJsonObject& text) const;
    QJsonObject record(int row) const;
    QString font() const;
    QString decode(const QJsonObject& record) const;
    // The glyph numbers a text spells (exactly `count` of them); throws.
    QVector<int> encode(const QString& text, int count) const;
    bool replaceRecord(int row, const QJsonObject& record, QString* error);

    AlienSoldierSession* session_;
    QString label_;
    int row_ = 0;
    QStringList characters_;
};

}  // namespace rl::alien_soldier
