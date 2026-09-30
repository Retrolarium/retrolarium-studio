#include "theme/window_frame.hpp"

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace rl::theme {

#ifdef Q_OS_WIN
namespace {

// Attribute ids from the Windows 11 SDK; MinGW headers may predate them.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kBorderColor = 34;
constexpr DWORD kCaptionColor = 35;
constexpr DWORD kTextColor = 36;

using SetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

// dwmapi.dll does not exist on Windows XP, which the static Qt kit still
// targets, so the function is resolved at run time instead of linked.
SetWindowAttribute dwmSetWindowAttribute() {
    static const SetWindowAttribute function = [] {
        HMODULE module = LoadLibraryW(L"dwmapi.dll");
        return module ? reinterpret_cast<SetWindowAttribute>(
                            reinterpret_cast<void*>(GetProcAddress(module, "DwmSetWindowAttribute")))
                      : nullptr;
    }();
    return function;
}

COLORREF colorRef(const QColor& color) {
    return RGB(color.red(), color.green(), color.blue());
}

}  // namespace
#endif

void applyWindowFrame(QWidget* window, const Tokens& tokens) {
#ifdef Q_OS_WIN
    const SetWindowAttribute DwmSetWindowAttribute = dwmSetWindowAttribute();
    if (!window || !DwmSetWindowAttribute) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = tokens.mode == Mode::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));
    const COLORREF caption = colorRef(tokens.bg);
    const COLORREF text = colorRef(tokens.muted);
    // An opaque hairline: DWM ignores alpha, so blend the line over the ground.
    const QColor& line = tokens.lineStrong;
    const double a = line.alphaF();
    const QColor border(static_cast<int>(line.red() * a + tokens.bg.red() * (1 - a)),
                        static_cast<int>(line.green() * a + tokens.bg.green() * (1 - a)),
                        static_cast<int>(line.blue() * a + tokens.bg.blue() * (1 - a)));
    const COLORREF borderRef = colorRef(border);
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, kBorderColor, &borderRef, sizeof(borderRef));
#else
    (void)window;
    (void)tokens;
#endif
}

}  // namespace rl::theme
