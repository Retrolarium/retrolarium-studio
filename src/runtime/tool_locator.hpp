#pragma once

#include "sdk/types.hpp"

#include <QString>
#include <QStringList>

namespace rl::runtime {

// Finds the programs the studio runs for plugins. Search orders:
//   kit:   $RETROLARIUM_KIT (rltool or rltool.py), kit/rltool.py next to the
//          studio (the installed layout), retrolarium-kit/rltool.py in the
//          studio's directory or up to six parents, `rltool` on PATH (an
//          .exe on Windows); rltool.py runs with python/python.exe next to
//          the studio, else host Python only with RETROLARIUM_DEV=1
//   mame:  $RETROLARIUM_MAME, the one chosen in Settings, the one the kit
//          remembers (`rltool movie mame`, in the user settings the kit and
//          the studio share), <studio>/mame/consoles.exe,
//          emulators/mame/mame/consoles.exe up to six parents
//   tools: next to the studio executable
class ToolLocator final {
public:
    // Fills program/arguments for a kit call; empty program and `error` set
    // when no kit is found. The environment carries RETROLARIUM_CORE and the
    // repository scripts the user consented to (RepositoryConsent).
    static sdk::ProcessSpec kitCommand(const QStringList& arguments, const QString& workingDirectory, QString* error);
    static QString emulatorPath(const QString& id);
    static QString toolPath(const QString& name);

    // A program on PATH, only with RETROLARIUM_DEV=1. On Windows only
    // an .exe is taken: a .cmd or .bat
    // runs through cmd.exe, whose parsing Qt's quoting does not cover, so an
    // argument holding & | ^ % (a ROM's file name) could run commands. A
    // .cmd or .bat found instead is named in `rejected`.
    static QString findOnPath(const QString& name, QString* rejected = nullptr);
    // Whether `program` starts directly, without a command interpreter.
    static bool isDirectLauncher(const QString& program);

private:
    static QString findInParents(const QString& relative);
};

}  // namespace rl::runtime
