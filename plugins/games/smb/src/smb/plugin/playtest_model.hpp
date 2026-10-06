#pragma once

#include "sdk/editor_model.hpp"

namespace rl::smb {

class SmbSession;

// The Playtest view of an SMB project: the build runs in MAME's NES driver
// from the start point chosen in the level editor.
class SmbPlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit SmbPlaytestModel(SmbSession* session);

    QString title() const override { return tr("Build and run"); }
    sdk::Inspection inspect() const override;

    QSize screenSize() const override { return {256, 240}; }
    QString coreLabel() const override { return QStringLiteral("MAME · nes"); }
    QString startLabel() const override;
    void play() override;
    bool isRecording() const override;

private:
    SmbSession* session_;
};

}  // namespace rl::smb
