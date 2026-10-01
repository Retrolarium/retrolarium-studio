#include "theme/stylesheet.hpp"

#include "theme/theme.hpp"

#include <QHash>
#include <QRegularExpression>

namespace rl::theme {

QString css(const QColor& color) {
    if (color.alpha() == 255) {
        return color.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

namespace {

// The sheet is written with @token placeholders so it reads like the CSS it
// came from; they are substituted in one pass.
const char* kTemplate = R"QSS(
QMainWindow, QDialog { background: @chrome; }
QMainWindow::separator { background: @line; width: 1px; height: 1px; }

QFrame[surface="bg"] { background: @bg; }
QFrame[surface="chrome"] { background: @chrome; }
QFrame[surface="panel"] { background: @panel; }
QFrame[surface="canvas"] { background: @canvas; }
QFrame[divider="line"] { background: @line; border: none; }
QFrame[divider="strong"] { background: @lineStrong; border: none; }

QLabel { background: transparent; color: @text; }
QLabel[tone="muted"] { color: @muted; }
QLabel[tone="faint"] { color: @faint; }
QLabel[tone="accent"] { color: @accentInk; }
QLabel[tone="onAccent"] { color: @onAccent; }

QMenuBar { background: @bg; color: @text; border: none; border-bottom: 1px solid @line; padding: 0 6px; }
QMenuBar::item { background: transparent; padding: 3px 9px; margin: 1px 0; }
QMenuBar::item:selected { background: @hover; }
QMenuBar::item:pressed { background: @accentSoft; color: @accentInk; }
QMenuBar[titlebar="true"] { background: transparent; border: none; padding: 0; }
QMenuBar[titlebar="true"]::item { padding: 5px 10px; margin: 0 1px 0 0; }
QMenu { background: @panel; color: @text; border: 1px solid @lineStrong; padding: 4px 0; }
QMenu::item { padding: 5px 28px 5px 12px; background: transparent; }
QMenu::item:selected { background: @accentSoft; color: @accentInk; }
QMenu::item:disabled { color: @faint; }
QMenu::separator { height: 1px; background: @line; margin: 4px 0; }
QMenu::icon { padding-left: 8px; }
QMenu::indicator { width: 12px; height: 12px; left: 6px; border: 1px solid @lineStrong; background: @field; }
QMenu::indicator:checked { background: @accent; border-color: @accent; image: url(@checkIcon); }

QToolTip { background: @panel; color: @text; border: 1px solid @lineStrong; padding: 4px 7px; }

QPushButton {
    background: transparent; color: @text;
    border: 1px solid @lineStrong; padding: 0 10px; min-height: 24px;
}
QPushButton:hover { background: @hover; }
QPushButton:pressed, QPushButton:checked { background: @accentSoft; color: @accentInk; }
QPushButton:disabled { color: @faint; border-color: @line; }
QPushButton[size="large"] { min-height: 26px; }
QPushButton[variant="flat"] { border-color: transparent; }
QPushButton[variant="dashed"] { border: 1px dashed @lineStrong; color: @faint; }
QPushButton[variant="dashed"]:hover { color: @text; border-color: @faint; }
QPushButton::menu-indicator { image: none; width: 0; }

QToolButton { background: transparent; border: none; color: @muted; padding: 0; }
QToolButton:hover { background: @hover; color: @text; }
QToolButton:pressed { background: @accentSoft; }
QToolButton:checked { background: @accentSoft; color: @accentInk; }
QToolButton:disabled { color: @faint; }
QToolButton::menu-indicator { image: none; width: 0; }
QToolButton[variant="rail"] { color: @muted; padding: 7px 0 6px 0; }
QToolButton[variant="rail"]:hover { background: transparent; color: @text; }
QToolButton[variant="rail"]:checked { background: @hover; color: @text; }
QToolButton[variant="segment"] { color: @muted; padding: 0 10px; }
QToolButton[variant="segment"]:hover { background: transparent; color: @text; }
QToolButton[variant="segment"]:checked { background: @accentSoft; color: @accentInk; }
QToolButton[variant="segmentIcon"] { padding: 0; }
QToolButton[variant="outlined"] { border: 1px solid @lineStrong; }
QToolButton[variant="outlined"]:hover { background: @hover; }
QToolButton[variant="segmentIcon"]:hover { background: transparent; }
QToolButton[variant="segmentIcon"]:checked { background: @accentSoft; }

QLabel[badge="platform"] { background: @accentSoft; color: @accentInk; padding: 0 5px; }
QLabel[badge="tag"] { border: 1px solid @lineStrong; color: @muted; padding: 0 6px; }
QLabel[badge="code"] { border: 1px solid @line; color: @faint; padding: 0 6px; }
QLabel[badge="accent"] { background: @accent; color: @onAccent; padding: 0 6px; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: @field; color: @text;
    border: 1px solid @lineStrong; padding: 0 8px; min-height: 24px;
    selection-background-color: @accentSoft; selection-color: @accentInk;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QComboBox:on { border-color: @accent; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @faint; border-color: @line; }
QLineEdit:read-only { color: @muted; }
QLineEdit[field="flat"] { background: transparent; border: none; padding: 0; min-height: 0; }
QComboBox { padding-right: 22px; }
QComboBox:hover { border-color: @faint; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px; border: none; }
QComboBox::down-arrow { image: url(@chevronDown); width: 10px; height: 10px; }
QComboBox QAbstractItemView {
    background: @panel; color: @text; border: 1px solid @lineStrong; outline: 0; padding: 2px 0;
    selection-background-color: @accentSoft; selection-color: @accentInk;
}
QComboBox QAbstractItemView::item { min-height: 24px; padding: 0 8px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }

QCheckBox, QRadioButton { spacing: 8px; color: @text; background: transparent; }
QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; border: 1px solid @lineStrong; background: @field; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: @accent; border-color: @accent; image: url(@checkIcon); }
QCheckBox:disabled, QRadioButton:disabled { color: @faint; }

QPlainTextEdit, QTextEdit {
    background: @field; color: @text; border: 1px solid @lineStrong;
    selection-background-color: @accentSoft; selection-color: @accentInk;
}
QPlainTextEdit[log="true"], QTextEdit[log="true"] { background: transparent; border: none; color: @muted; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget#qt_scrollarea_viewport { background: transparent; }
QAbstractScrollArea::corner { background: transparent; border: none; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; border: none; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: @lineStrong; min-height: 28px; margin: 2px 2px 2px 3px; }
QScrollBar::handle:horizontal { background: @lineStrong; min-width: 28px; margin: 3px 2px 2px 2px; }
QScrollBar::handle:hover { background: @faint; }
QScrollBar::handle:pressed { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QSplitter::handle { background: @line; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QHeaderView { background: @chrome; border: none; }
QHeaderView::section {
    background: @chrome; color: @muted; border: none; border-bottom: 1px solid @line;
    padding: 7px 14px;
}
QTableView, QTreeView, QListView {
    background: transparent; color: @text; border: none; outline: 0; gridline-color: @line;
    selection-background-color: @accentSoft; selection-color: @accentInk;
}
QTableView::item { border: none; border-bottom: 1px solid @line; padding: 0 14px; }
QTableView::item:hover, QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: @accentSoft; color: @accentInk; }
QTreeView::item { min-height: 26px; }
QTreeView::branch { background: transparent; }

QTabWidget::pane { border: none; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @muted; border: none; border-bottom: 2px solid transparent;
    padding: 0 10px; min-height: 26px;
}
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; border-bottom-color: @accent; }

QProgressBar { background: @lineStrong; border: none; max-height: 4px; min-height: 4px; color: transparent; text-align: center; }
QProgressBar::chunk { background: @accent; }

QSlider::groove:horizontal { height: 2px; background: @lineStrong; }
QSlider::sub-page:horizontal { background: @accent; }
QSlider::handle:horizontal { width: 8px; height: 14px; margin: -6px 0; background: @accent; }
QSlider::handle:horizontal:hover { background: @accentInk; }

QStatusBar { background: @chrome; color: @muted; border-top: 1px solid @line; }
QStatusBar::item { border: none; }

QDockWidget { color: @text; }
QDockWidget::title { background: @chrome; padding: 6px 10px; }
)QSS";

}  // namespace

QString buildStyleSheet(const Tokens& t, const Theme& theme) {
    const QString suffix = modeName(t.mode);
    const QHash<QString, QString> values = {
        {QStringLiteral("bg"), css(t.bg)},
        {QStringLiteral("chrome"), css(t.chrome)},
        {QStringLiteral("panel"), css(t.panel)},
        {QStringLiteral("field"), css(t.field)},
        {QStringLiteral("text"), css(t.text)},
        {QStringLiteral("muted"), css(t.muted)},
        {QStringLiteral("faint"), css(t.faint)},
        {QStringLiteral("line"), css(t.line)},
        {QStringLiteral("lineStrong"), css(t.lineStrong)},
        {QStringLiteral("accent"), css(t.accent)},
        {QStringLiteral("accentInk"), css(t.accentInk)},
        {QStringLiteral("accentSoft"), css(t.accentSoft)},
        {QStringLiteral("hover"), css(t.hover)},
        {QStringLiteral("canvas"), css(t.canvas)},
        {QStringLiteral("onAccent"), css(t.onAccent)},
        {QStringLiteral("mark"), css(t.mark)},
        {QStringLiteral("chevronDown"),
         theme.assetPath(QStringLiteral("chevron-down-%1.svg").arg(suffix))},
        {QStringLiteral("chevronUp"),
         theme.assetPath(QStringLiteral("chevron-up-%1.svg").arg(suffix))},
        {QStringLiteral("checkIcon"), theme.assetPath(QStringLiteral("check-%1.svg").arg(suffix))},
    };

    static const QRegularExpression token(QStringLiteral("@([A-Za-z]+)"));
    const QString source = QString::fromLatin1(kTemplate);
    QString result;
    result.reserve(source.size() + 2048);
    int last = 0;
    auto it = token.globalMatch(source);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += source.midRef(last, match.capturedStart() - last);
        const auto value = values.constFind(match.captured(1));
        result += value != values.constEnd() ? *value : match.captured(0);
        last = match.capturedEnd();
    }
    result += source.midRef(last);
    return result;
}

}  // namespace rl::theme
