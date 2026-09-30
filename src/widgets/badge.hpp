#pragma once

#include <QLabel>

namespace rl::ui {

// Small fixed-height labels: the platform badge ("NES"), a revision tag
// ("JU"), a code tag naming an editor or codec, or an accent callout.
class Badge final : public QLabel {
    Q_OBJECT

public:
    enum class Kind { Platform, Tag, Code, Accent };

    explicit Badge(const QString& text, Kind kind = Kind::Tag, QWidget* parent = nullptr);

    void setKind(Kind kind);
    Kind kind() const { return kind_; }

private:
    Kind kind_;
};

}  // namespace rl::ui
