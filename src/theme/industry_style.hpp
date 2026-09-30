#pragma once

#include <QProxyStyle>

namespace rl::theme {

// Fusion with the Industry metrics: square geometry, thin scroll bars,
// hairline splitters and no dotted focus rectangles. Colors come from the
// application palette and style sheet, never from here.
class IndustryStyle final : public QProxyStyle {
    Q_OBJECT

public:
    IndustryStyle();

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;
};

}  // namespace rl::theme
