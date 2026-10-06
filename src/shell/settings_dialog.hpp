#pragma once

#include "frame/studio_dialog.hpp"

class QComboBox;

namespace rl::ui {
class Label;
}

namespace rl::shell {

// File > Settings: the interface language, the MAME the studio plays games
// in, and the Famicom Disk System BIOS that MAME needs for disk games.
class SettingsDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // QSettings keys.
    static constexpr const char* kLanguageKey = "ui/language";
    static constexpr const char* kMameKey = "tools/mame";

private:
    void chooseMame();
    void chooseBios();
    void refresh();

    QComboBox* language_;
    ui::Label* mame_;
    ui::Label* bios_;
};

}  // namespace rl::shell
