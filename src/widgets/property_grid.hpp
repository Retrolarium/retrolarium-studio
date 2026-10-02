#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QWidget>

class QGridLayout;

namespace rl::ui {

// A single-line value field with a faint hint at the right edge: a unit
// ("px", "metatile"), a range ("0-7") or "auto".
class PropertyField final : public QLineEdit {
    Q_OBJECT

public:
    explicit PropertyField(const QString& value = QString(), const QString& hint = QString(),
                           QWidget* parent = nullptr);

    void setHint(const QString& hint);
    QString hint() const { return hint_; }

    // Monospace digits for offsets and hex values.
    void setMonospace(bool monospace);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateMargins();

    QString hint_;
};

// The two-column key/value grid of the inspector: 88px muted keys, fields
// that stretch.
class PropertyGrid final : public QWidget {
    Q_OBJECT

public:
    explicit PropertyGrid(QWidget* parent = nullptr);

    void addRow(const QString& key, QWidget* field);
    PropertyField* addField(const QString& key, const QString& value,
                            const QString& hint = QString());
    QComboBox* addChoice(const QString& key, const QStringList& choices, int current = 0,
                         const QVector<QImage>& icons = {});

    void setKeyWidth(int pixels);
    void clear();
    int rowCount() const { return rows_; }

private:
    QGridLayout* grid_;
    int rows_ = 0;
    int keyWidth_ = 88;
};

// Monospace key/value rows of raw engine data (record, offset, encoding).
class EngineTable final : public QWidget {
    Q_OBJECT

public:
    explicit EngineTable(QWidget* parent = nullptr);

    void addRow(const QString& key, const QString& value);
    void clear();

private:
    QGridLayout* grid_;
    int rows_ = 0;
};

}  // namespace rl::ui
