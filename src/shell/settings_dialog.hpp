#pragma once

#include "frame/studio_dialog.hpp"

class QComboBox;
class QPushButton;

namespace rl::runtime {
class ProcessRunner;
}

namespace rl::ui {
class Label;
}

namespace rl::shell {

// File > Settings: the interface language, the MAME the studio plays games
// in, and the Famicom Disk System BIOS that MAME needs for disk games.
class SettingsDialog final : public frame::StudioDialog {
    Q_OBJECT

public:
    // `processes`: the studio's process runner, which installs a BIOS dump
    // through the kit (one kit process at a time, its environment).
    explicit SettingsDialog(QWidget* parent = nullptr, runtime::ProcessRunner* processes = nullptr);

    // QSettings keys.
    static constexpr const char* kLanguageKey = "ui/language";
    static constexpr const char* kMameKey = "tools/mame";

    // The interface language: the one chosen here, else the system's when
    // the studio has it, else English.
    static QString language();

signals:
    // Restart now: the dialog has closed; the studio should restart into the
    // new language.
    void restartRequested();

private:
    void chooseMame();
    void chooseBios();
    void refresh();

    runtime::ProcessRunner* processes_;
    QComboBox* language_;
    ui::Label* mame_;
    ui::Label* bios_;
    QPushButton* chooseBios_ = nullptr;
};

}  // namespace rl::shell
