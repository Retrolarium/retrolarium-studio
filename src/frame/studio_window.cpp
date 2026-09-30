#include "frame/studio_window.hpp"

#include "frame/title_bar.hpp"
#include "theme/theme.hpp"
#include "theme/window_frame.hpp"

#include <QMenuBar>
#include <QShowEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace rl::frame {

#ifdef Q_OS_WIN
namespace {

// Vista+ metric; the kit builds against the XP API level, where the headers
// do not declare it. Systems without it return 0, which is the right answer.
constexpr int kPaddedBorder = 92;  // SM_CXPADDEDBORDER

using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);

// Per-monitor metrics exist from Windows 10 1607; older systems, including
// the XP floor of the static Qt kit, fall back to the system-wide values.
int systemMetric(HWND hwnd, int index) {
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    static const auto getSystemMetricsForDpi = reinterpret_cast<GetSystemMetricsForDpiFn>(
        reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi")));
    if (getDpiForWindow && getSystemMetricsForDpi) {
        return getSystemMetricsForDpi(index, getDpiForWindow(hwnd));
    }
    return GetSystemMetrics(index);
}

// Thickness of the frame the system hides off screen when maximizing.
int maximizedOverhang(HWND hwnd) {
    return systemMetric(hwnd, SM_CYSIZEFRAME) + systemMetric(hwnd, kPaddedBorder);
}

// The top edge has no non-client frame any more, so a strip of the title bar
// takes over resizing, as thin as the system sizing border.
int topResizeBorder(HWND hwnd) {
    return systemMetric(hwnd, SM_CYSIZEFRAME);
}

void showSystemMenu(HWND hwnd, int x, int y) {
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) {
        return;
    }
    const bool maximized = IsZoomed(hwnd) != 0;
    const UINT on = MF_BYCOMMAND | MF_ENABLED;
    const UINT off = MF_BYCOMMAND | MF_GRAYED;
    EnableMenuItem(menu, SC_RESTORE, maximized ? on : off);
    EnableMenuItem(menu, SC_MOVE, maximized ? off : on);
    EnableMenuItem(menu, SC_SIZE, maximized ? off : on);
    EnableMenuItem(menu, SC_MINIMIZE, on);
    EnableMenuItem(menu, SC_MAXIMIZE, maximized ? off : on);
    EnableMenuItem(menu, SC_CLOSE, on);
    SetMenuDefaultItem(menu, SC_CLOSE, FALSE);
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, hwnd, nullptr));
    if (command != 0) {
        PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
    }
}

}  // namespace
#endif

StudioWindow::StudioWindow(QWidget* parent) : QMainWindow(parent), titleBar_(new TitleBar(this)) {
    setMenuWidget(titleBar_);
    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeRequested, this, &StudioWindow::toggleMaximized);
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);

#ifdef Q_OS_WIN
    customFrame_ = true;
    // Create the native window now, so the caption is gone before the first
    // geometry is applied and Qt computes sizes with the final margins.
    applyNativeFrame();
#else
    titleBar_->setWindowButtonsVisible(false);
#endif
}

QMenuBar* StudioWindow::menus() const {
    return titleBar_->menuBar();
}

void StudioWindow::toggleMaximized() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void StudioWindow::applyNativeFrame() {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_NOOWNERZORDER);
#endif
}

void StudioWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    theme::applyWindowFrame(this, theme::current());
}

bool StudioWindow::nativeEvent(const QByteArray& eventType, void* message, long* result) {
#ifdef Q_OS_WIN
    if (!customFrame_ || eventType != "windows_generic_MSG") {
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    MSG* msg = static_cast<MSG*>(message);
    const HWND hwnd = msg->hwnd;

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (!msg->wParam) {
            break;
        }
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        const LONG originalTop = params->rgrc[0].top;
        // Let the system size the side and bottom borders, then give the
        // caption back to the client area.
        const LRESULT defaultResult =
            DefWindowProcW(hwnd, WM_NCCALCSIZE, msg->wParam, msg->lParam);
        if (defaultResult != 0) {
            *result = defaultResult;
            return true;
        }
        params->rgrc[0].top = originalTop;
        if (IsZoomed(hwnd)) {
            // A maximized window overhangs the monitor by its frame.
            params->rgrc[0].top += maximizedOverhang(hwnd);
        }
        *result = 0;
        return true;
    }

    case WM_NCHITTEST: {
        const LRESULT defaultHit = DefWindowProcW(hwnd, WM_NCHITTEST, msg->wParam, msg->lParam);
        if (defaultHit != HTCLIENT) {
            *result = defaultHit;  // side and bottom borders, corners
            return true;
        }
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(hwnd, &point);
        if (!IsZoomed(hwnd) && point.y < topResizeBorder(hwnd)) {
            *result = HTTOP;
            return true;
        }
        const double dpr = devicePixelRatioF();
        const QPoint local(static_cast<int>(point.x / dpr), static_cast<int>(point.y / dpr));
        const QPoint inBar = titleBar_->mapFrom(this, local);
        if (titleBar_->isVisible() && titleBar_->rect().contains(inBar)) {
            const TitleBar::Button button = titleBar_->buttonAt(inBar);
            if (button == TitleBar::Button::Maximize) {
                *result = HTMAXBUTTON;  // Windows 11 shows snap layouts on hover
                return true;
            }
            if (button == TitleBar::Button::None && titleBar_->isDragArea(inBar)) {
                *result = HTCAPTION;
                return true;
            }
        }
        *result = HTCLIENT;
        return true;
    }

    // The maximize button is non-client for the system, so its hover and
    // clicks arrive here instead of as Qt mouse events. Consuming them keeps
    // DefWindowProc from drawing a legacy caption button.
    case WM_NCMOUSEMOVE: {
        if (msg->wParam == HTMAXBUTTON) {
            titleBar_->setNativeHover(TitleBar::Button::Maximize);
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE | TME_NONCLIENT;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            *result = 0;
            return true;
        }
        titleBar_->setNativeHover(TitleBar::Button::None);
        break;
    }
    case WM_NCMOUSELEAVE:
        titleBar_->setNativeHover(TitleBar::Button::None);
        titleBar_->setNativePressed(TitleBar::Button::None);
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (msg->wParam == HTMAXBUTTON) {
            titleBar_->setNativePressed(TitleBar::Button::Maximize);
            *result = 0;
            return true;
        }
        break;
    case WM_NCLBUTTONUP:
        if (msg->wParam == HTMAXBUTTON) {
            titleBar_->setNativePressed(TitleBar::Button::None);
            toggleMaximized();
            *result = 0;
            return true;
        }
        titleBar_->setNativePressed(TitleBar::Button::None);
        break;
    case WM_NCRBUTTONUP:
        if (msg->wParam == HTCAPTION) {
            showSystemMenu(hwnd, GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            *result = 0;
            return true;
        }
        break;
    default:
        break;
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

}  // namespace rl::frame
