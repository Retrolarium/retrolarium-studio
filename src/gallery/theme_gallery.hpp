#pragma once

#include "frame/studio_window.hpp"

namespace rl::gallery {

// Every themed component on one page, with a live theme switch. It is the
// visual test bench of the design system: nothing here knows about games.
class ThemeGallery final : public frame::StudioWindow {
    Q_OBJECT

public:
    explicit ThemeGallery(QWidget* parent = nullptr);

private:
    void buildMenus();
    QWidget* buildToolbar();
    QWidget* buildPage();
};

}  // namespace rl::gallery
