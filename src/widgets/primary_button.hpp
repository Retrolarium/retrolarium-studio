#pragma once

#include <QAbstractButton>

namespace rl::ui {

// The one accent-filled action of a surface ("Play from 1-1"), framed by
// registration marks on two corners. The widget reserves a margin around the
// button face so the marks are never clipped.
class PrimaryButton final : public QAbstractButton {
    Q_OBJECT

public:
    explicit PrimaryButton(const QString& text, const QString& iconName = QString(),
                           QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void setIconName(const QString& name);
    // Red instead of the accent, for an action that replaces data.
    void setDanger(bool danger);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool hitButton(const QPoint& pos) const override;

private:
    QRect faceRect() const;

    QString iconName_;
    bool hovered_ = false;
    bool danger_ = false;
};

}  // namespace rl::ui
