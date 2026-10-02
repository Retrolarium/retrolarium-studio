#pragma once

#include "frame/studio_dialog.hpp"

namespace rl::shell {

// Help > About: what the studio is, the games its plugins open, how an edit
// reaches the running game, and the keys worth knowing.
class AboutDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace rl::shell
