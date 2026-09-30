#pragma once

#include <QMainWindow>

namespace rl::gallery {

// Every themed component on one page, with a live theme switch. It is the
// visual test bench of the design system: nothing here knows about games.
class ThemeGallery final : public QMainWindow {
    Q_OBJECT

public:
    explicit ThemeGallery(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildMenus();
    QWidget* buildToolbar();
    QWidget* buildPage();
};

}  // namespace rl::gallery
