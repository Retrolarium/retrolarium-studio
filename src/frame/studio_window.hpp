#pragma once

#include "frame/native_frame.hpp"

#include <QMainWindow>

class QMenuBar;

namespace rl::frame {

class TitleBar;

// A top-level window with the studio title bar in place of the system
// caption (see NativeFrame). Where the platform keeps its decorations the
// title bar only carries the menus.
class StudioWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit StudioWindow(QWidget* parent = nullptr);

    TitleBar* titleBar() const { return titleBar_; }
    // The menu bar embedded in the title bar. Use this, not QMainWindow::menuBar(),
    // which would replace the title bar with a plain menu bar.
    QMenuBar* menus() const;

    // True when this window draws its own caption.
    bool customFrame() const { return customFrame_; }

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    void showEvent(QShowEvent* event) override;

private:
    TitleBar* titleBar_;
    NativeFrame frame_;
    bool customFrame_ = false;
};

}  // namespace rl::frame
