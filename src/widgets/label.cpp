#include "widgets/label.hpp"

#include "theme/typography.hpp"

#include <QStyle>
#include <QVariant>

namespace rl::ui {

using theme::TextRole;
namespace size = theme::size;

Label::Label(const QString& text, Role role, QWidget* parent) : QLabel(text, parent), role_(role) {
    setRole(role);
}

void Label::setRole(Role role) {
    role_ = role;
    QFont f;
    QString tone = QStringLiteral("text");
    switch (role) {
    case Role::Body:
        f = theme::font(TextRole::Body, size::kBody);
        break;
    case Role::Row:
        f = theme::font(TextRole::Body, size::kRow);
        break;
    case Role::Small:
        f = theme::font(TextRole::Body, size::kSmall);
        tone = QStringLiteral("muted");
        break;
    case Role::Meta:
        f = theme::font(TextRole::Body, size::kMeta);
        tone = QStringLiteral("faint");
        break;
    case Role::Caps:
        f = theme::font(TextRole::Caps, size::kCaps);
        tone = QStringLiteral("faint");
        break;
    case Role::CapsMuted:
        f = theme::font(TextRole::Caps, 12.0);
        tone = QStringLiteral("muted");
        break;
    case Role::PanelTitle:
        f = theme::font(TextRole::Caps, size::kPanelTitle);
        theme::setLetterSpacingEm(f, 0.02);
        break;
    case Role::Title:
        f = theme::font(TextRole::Heading, size::kTitle);
        break;
    case Role::Project:
        f = theme::font(TextRole::Heading, size::kProject);
        theme::setLetterSpacingEm(f, -0.01);
        break;
    case Role::Mono:
        f = theme::font(TextRole::Mono, 11.5);
        break;
    case Role::MonoSmall:
        f = theme::font(TextRole::Mono, 10.5);
        tone = QStringLiteral("faint");
        break;
    }
    setFont(f);
    setTone(tone);
}

void Label::setTone(const QString& tone) {
    setProperty("tone", tone);
    style()->unpolish(this);
    style()->polish(this);
}

Divider::Divider(Qt::Orientation orientation, Strength strength, QWidget* parent)
    : QFrame(parent) {
    setProperty("divider",
                strength == Strength::Strong ? QStringLiteral("strong") : QStringLiteral("line"));
    setFrameShape(QFrame::NoFrame);
    if (orientation == Qt::Horizontal) {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        setFixedWidth(1);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
}

}  // namespace rl::ui
