#pragma once

namespace rl::frame {

// Where a DLL named without a path is looked for: System32 and, with
// `applicationDirectory`, the program's own folder; never the current folder
// or PATH. A setup.exe started from Downloads would otherwise load a
// version.dll or dwmapi.dll left beside it. Called first in main(), before
// anything loads a library at run time. Does nothing outside Windows.
void restrictDllSearch(bool applicationDirectory);

}  // namespace rl::frame
