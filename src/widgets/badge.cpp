#include "widgets/badge.hpp"

#include "theme/typography.hpp"

#include <QStyle>
#include <QVariant>

namespace rl::ui {

Badge::Badge(const QString& text, Kind kind, QWidget* parent) : QLabel(text, parent), kind_(kind) {
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKind(kind);
}

void Badge::setKind(Kind kind) {
    kind_ = kind;
    QString name;
    switch (kind) {
    case Kind::Platform:
        name = QStringLiteral("platform");
        setFont(theme::font(theme::TextRole::MonoSemiBold, 10.0));
        setFixedHeight(22);
        break;
    case Kind::Tag:
        name = QStringLiteral("tag");
        setFont(theme::font(theme::TextRole::Body, 11.0));
        setFixedHeight(18);
        break;
    case Kind::Code:
        name = QStringLiteral("code");
        setFont(theme::font(theme::TextRole::Mono, 10.5));
        setFixedHeight(18);
        break;
    case Kind::Accent:
        name = QStringLiteral("accent");
        setFont(theme::font(theme::TextRole::MonoSemiBold, 10.5));
        setFixedHeight(18);
        break;
    }
    setProperty("badge", name);
    style()->unpolish(this);
    style()->polish(this);
}

}  // namespace rl::ui
