#include "theme/industry_style.hpp"

#include <QStyleFactory>

namespace rl::theme {

IndustryStyle::IndustryStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

int IndustryStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const {
    switch (metric) {
        case PM_ScrollBarExtent:
            return 10;
        case PM_SplitterWidth:
            return 1;
        case PM_ToolBarIconSize:
            return 17;
        case PM_SmallIconSize:
        case PM_ButtonIconSize:
            return 15;
        case PM_ListViewIconSize:
            return 16;
        case PM_TabBarIconSize:
            return 14;
        case PM_MenuBarPanelWidth:
        case PM_MenuBarHMargin:
        case PM_MenuBarVMargin:
        case PM_ToolBarFrameWidth:
        case PM_ToolBarItemMargin:
        case PM_DockWidgetFrameWidth:
            return 0;
        case PM_DefaultFrameWidth:
        case PM_MenuPanelWidth:
            return 1;
        case PM_ToolBarItemSpacing:
            return 2;
        case PM_ToolTipLabelFrameWidth:
            return 1;
        case PM_FocusFrameHMargin:
        case PM_FocusFrameVMargin:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int IndustryStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                             QStyleHintReturn* returnData) const {
    switch (hint) {
        case SH_ComboBox_Popup:
            return 0;  // a plain list, not the mac-style popup over the field
        case SH_ComboBox_ListMouseTracking:
        case SH_Menu_MouseTracking:
        case SH_MenuBar_MouseTracking:
            return 1;
        case SH_ItemView_ShowDecorationSelected:
            return 1;
        case SH_ToolTipLabel_Opacity:
            return 255;
        case SH_DialogButtonBox_ButtonsHaveIcons:
            return 0;
        case SH_Menu_FlashTriggeredItem:
            return 0;
        case SH_ToolTip_WakeUpDelay:
            return 500;
        default:
            return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

void IndustryStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                  const QWidget* widget) const {
    if (element == PE_FrameFocusRect) {
        // Focus is shown by the accent border from the style sheet.
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

}  // namespace rl::theme
