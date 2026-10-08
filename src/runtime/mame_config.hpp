#pragma once

#include <QString>
#include <QVector>

namespace rl::runtime::mame {

// A feature MAME lists as imperfect or unemulated for a system: the reason
// for its "There are known problems with this system" screen.
struct Feature {
    QString device;     // device short name: "nes"
    QString type;       // "graphics", "sound", ...
    QString status;     // "imperfect" or "unemulated"
};

// The features MAME warns about for a system, from `-listxml <system>`.
// Cached per program and system.
QVector<Feature> warnedFeatures(const QString& program, const QString& system, QString* error);

// Marks the warning for these features as shown just now in the system's
// configuration (cfg/<system>.cfg), so MAME's "skip_warnings" setting skips
// it: the warning waits for a key press that cannot reach an embedded MAME.
// Everything else in the file is kept.
bool acknowledgeWarnings(const QString& configDirectory, const QString& system, const QVector<Feature>& features,
                         QString* error);

// MAME honours that mark only with its "skip_warnings" UI option on, which
// is off unless a ui.ini says otherwise. Turns it on in the ui.ini MAME reads
// from its working directory (./ui.ini, else ini/ui.ini, the first two places
// of the default inipath), creating ./ui.ini with just that line when there
// is none; other settings are kept. `changed` says whether the file changed.
bool enableSkipWarnings(const QString& workingDirectory, bool* changed, QString* error);

}  // namespace rl::runtime::mame
