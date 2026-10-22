#include "frame/studio_window.hpp"

#include "frame/title_bar.hpp"
#include "theme/theme.hpp"
#include "theme/window_frame.hpp"

#include <QMenuBar>
#include <QShowEvent>

namespace rl::frame {

StudioWindow::StudioWindow(QWidget* parent)
    : QMainWindow(parent), titleBar_(new TitleBar(this)), frame_(this, titleBar_) {
    setMenuWidget(titleBar_);
    connect(titleBar_, &TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBar::maximizeRequested, this, [this] { frame_.toggleMaximized(); });
    connect(titleBar_, &TitleBar::closeRequested, this, &QWidget::close);

    customFrame_ = NativeFrame::supported();
    if (customFrame_) {
        frame_.apply();
    } else {
        titleBar_->setWindowButtonsVisible(false);
    }
}

QMenuBar* StudioWindow::menus() const { return titleBar_->menuBar(); }

void StudioWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    theme::applyWindowFrame(this, theme::current());
}

bool StudioWindow::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    if (customFrame_ && frame_.handle(eventType, message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

}  // namespace rl::frame
