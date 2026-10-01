#pragma once

#include <QByteArray>

class QWidget;

namespace rl::frame {

class TitleBar;

// The Windows side of the studio frame, shared by windows and dialogs.
//
// The native frame is kept (snapping, shadow, resize borders, maximize
// animation, Windows 11 snap layouts): only the caption is removed in
// WM_NCCALCSIZE, and WM_NCHITTEST maps the title bar back to caption, resize
// and maximize-button hits. Qt 5.15 reads the frame margins back from the
// WM_NCCALCSIZE result, so its geometry stays consistent. Elsewhere it does
// nothing and the platform decorations stay.
class NativeFrame {
public:
    NativeFrame(QWidget* window, TitleBar* titleBar);

    // True where the caption is drawn by the title bar.
    static bool supported();

    // Recomputes the frame, so the caption is gone before the first geometry
    // is applied. Creates the native window.
    void apply();

    // Handles one native message; true when it was consumed.
    bool handle(const QByteArray& eventType, void* message, long* result);

    void toggleMaximized();

private:
    QWidget* window_;
    TitleBar* titleBar_;
};

}  // namespace rl::frame
