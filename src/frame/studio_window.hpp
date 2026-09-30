#pragma once

#include <QMainWindow>

class QMenuBar;

namespace rl::frame {

class TitleBar;

// A top-level window with the studio title bar in place of the system
// caption.
//
// On Windows the native frame is kept (snapping, shadow, resize borders,
// maximize animation, Windows 11 snap layouts): only the caption is removed in
// WM_NCCALCSIZE, and WM_NCHITTEST maps the title bar back to caption, resize
// and maximize-button hits. Qt 5.15 reads the frame margins back from the
// WM_NCCALCSIZE result, so its geometry stays consistent. Elsewhere the
// platform decorations stay and the title bar only carries the menus.
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
    void toggleMaximized();
    void applyNativeFrame();

    TitleBar* titleBar_;
    bool customFrame_ = false;
};

}  // namespace rl::frame
