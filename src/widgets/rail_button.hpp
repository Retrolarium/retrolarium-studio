#pragma once

#include <QAbstractButton>

namespace rl::ui {

// One data kind in the left rail: a 19px glyph over a 9.5px label. The
// checked kind gets the hover fill and a 2px accent bar inset from the ends.
class RailButton final : public QAbstractButton {
    Q_OBJECT

public:
    RailButton(const QString& iconName, const QString& label, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString iconName_;
    bool hovered_ = false;
};

}  // namespace rl::ui
