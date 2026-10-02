#pragma once

#include <QString>
#include <QWidget>

class QTimer;

namespace rl::ui {

// A short notice floating over the bottom of its parent: why an action did
// nothing ("The area is full"). Hides itself after a few seconds or on a
// click; a new message replaces the old one.
class Toast final : public QWidget {
    Q_OBJECT

public:
    explicit Toast(QWidget* parent);

    void showMessage(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QString text_;
    QTimer* timer_;
};

}  // namespace rl::ui
