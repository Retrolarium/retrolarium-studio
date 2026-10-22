#pragma once

#include <QFrame>
#include <QLabel>

namespace rl::ui {

// Text with a design role. The role fixes font and color together so callers
// never pick sizes by hand.
class Label final : public QLabel {
    Q_OBJECT

public:
    enum class Role {
        Body,        // 13px text
        Row,         // 12.5px text
        Small,       // 12px muted
        Meta,        // 11px faint
        Caps,        // section header: condensed 11.5px uppercase faint
        CapsMuted,   // table/sub header: condensed 12px uppercase muted
        PanelTitle,  // document list title: condensed 15px uppercase
        Title,       // inspector title: condensed 21px
        Project,     // project switcher name: condensed 18px
        Mono,        // 11.5px data
        MonoSmall,   // 10.5px data, faint
    };

    explicit Label(const QString& text, Role role = Role::Body, QWidget* parent = nullptr);

    void setRole(Role role);
    Role role() const { return role_; }

    // Overrides the role color: "text", "muted", "faint", "accent".
    void setTone(const QString& tone);

private:
    Role role_;
};

// 1px separator in the hairline color.
class Divider final : public QFrame {
    Q_OBJECT

public:
    enum class Strength { Line, Strong };

    explicit Divider(Qt::Orientation orientation, Strength strength = Strength::Line, QWidget* parent = nullptr);
};

}  // namespace rl::ui
