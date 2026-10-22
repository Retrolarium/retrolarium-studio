#pragma once

#include "sdk/types.hpp"

#include <QString>
#include <QStringList>

namespace rl::runtime {

// Finds the programs the studio runs for plugins. Search orders:
//   kit:   $RETROLARIUM_KIT (rltool or rltool.py), kit/rltool.py next to the
//          studio (the installed layout), retrolarium-kit/rltool.py in the
//          studio's directory or up to six parents, `rltool` on PATH;
//          rltool.py runs with python/python.exe next to the studio, else
//          Python on PATH
//   mame:  $RETROLARIUM_MAME, the one chosen in Settings, the one the kit
//          remembers (`rltool movie mame`, in the user settings the kit and
//          the studio share), <studio>/mame/consoles.exe,
//          emulators/mame/mame/consoles.exe up to six parents
//   tools: next to the studio executable
class ToolLocator final {
public:
    // Fills program/arguments for a kit call; empty program and `error` set
    // when no kit is found.
    static sdk::ProcessSpec kitCommand(const QStringList& arguments, const QString& workingDirectory, QString* error);
    static QString emulatorPath(const QString& id);
    static QString toolPath(const QString& name);

private:
    static QString findInParents(const QString& relative);
};

}  // namespace rl::runtime
