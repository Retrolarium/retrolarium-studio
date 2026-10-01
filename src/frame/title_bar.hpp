#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMenuBar;

namespace rl::frame {

// The application title bar: app mark, menus, centered title and the window
// buttons, all in the studio typography. It replaces the system caption; the
// window it sits in asks it which points are draggable.
class TitleBar final : public QWidget {
    Q_OBJECT

public:
    enum class Button { None, Minimize, Maximize, Close };

    explicit TitleBar(QWidget* window);

    QMenuBar* menuBar() const { return menuBar_; }

    void setTitle(const QString& primary, const QString& secondary = QString());

    // Extra controls placed left of the window buttons (theme toggle, account).
    void addTrailingWidget(QWidget* widget);

    // Whether the window buttons are drawn. Off where the platform keeps its
    // own decorations.
    void setWindowButtonsVisible(bool visible);
    bool windowButtonsVisible() const { return buttonsVisible_; }

    // Dialog bars: no menus, the title next to the mark, only Close.
    void setDialogStyle();

    // Hit testing in title bar coordinates.
    Button buttonAt(const QPoint& pos) const;
    bool isDragArea(const QPoint& pos) const;

    // Hover/press state driven by the native frame for the maximize button,
    // which Windows reports through non-client messages to show snap layouts.
    void setNativeHover(Button button);
    void setNativePressed(Button button);

    static constexpr int kHeight = 40;

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool windowActive() const;
    QRect buttonRect(Button button) const;
    int buttonsWidth() const;
    void paintButton(QPainter& painter, Button button) const;
    void trigger(Button button);

    QWidget* window_;
    QHBoxLayout* layout_;
    QWidget* mark_;
    QMenuBar* menuBar_;
    QWidget* trailing_;
    QHBoxLayout* trailingLayout_;
    QString primary_;
    QString secondary_;
    bool buttonsVisible_ = true;
    bool dialog_ = false;
    Button hovered_ = Button::None;
    Button pressed_ = Button::None;
};

}  // namespace rl::frame
