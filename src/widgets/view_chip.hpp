#pragma once

#include <QAbstractButton>
#include <QImage>

class QMenu;

namespace rl::ui {

// A view setting shown as "Key Value" in a hairline box (Grid On, Zoom 2x).
// View chips never change document data. A chip either toggles (checkable) or
// opens a menu of choices.
class ViewChip final : public QAbstractButton {
    Q_OBJECT

public:
    ViewChip(const QString& key, const QString& value, QWidget* parent = nullptr);

    QString key() const { return key_; }
    QString value() const { return value_; }
    void setKey(const QString& key);
    void setValue(const QString& value);

    // Choices shown in a drop-down; picking one sets the value and emits.
    void setChoices(const QStringList& choices);
    // A small picture before the key (the game's player on the start chip),
    // its transparent edges cut and scaled up in whole steps to the chip.
    void setPicture(const QImage& picture);
    // Pressing and dragging the chip drags this (a library item onto a map)
    // instead of clicking it.
    void setDragData(const QString& mimeType, const QByteArray& data);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueChosen(const QString& value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void showChoices();

    QString key_;
    QString value_;
    QStringList choices_;
    QImage picture_;
    bool hovered_ = false;
    QString dragType_;
    QByteArray dragData_;
    QPoint pressPos_;
};

}  // namespace rl::ui
