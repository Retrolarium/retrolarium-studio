#include "widgets/brand_mark.hpp"

#include "theme/brand.hpp"
#include "theme/theme.hpp"

#include <QPainter>

namespace rl::ui {

BrandMark::BrandMark(int size, QWidget* parent) : QWidget(parent), size_(size) {
    setFixedSize(size, size);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, qOverload<>(&QWidget::update));
}

void BrandMark::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawPixmap(0, 0, theme::brand::mark(theme::Theme::instance().mode(), size_, devicePixelRatioF()));
}

}  // namespace rl::ui
