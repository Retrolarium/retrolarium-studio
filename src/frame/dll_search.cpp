#include "frame/dll_search.hpp"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>

// Flags from the Windows 8 SDK (KB2533623 on Windows 7); MinGW headers may
// declare them only for a newer _WIN32_WINNT.
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
#endif

namespace rl::frame {

void restrictDllSearch(bool applicationDirectory) {
#ifdef Q_OS_WIN
    // Resolved at run time: Windows 7 has it only with KB2533623.
    using SetDefaultDllDirectoriesFunction = BOOL(WINAPI*)(DWORD);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const SetDefaultDllDirectoriesFunction setDefaultDllDirectories =
        kernel ? reinterpret_cast<SetDefaultDllDirectoriesFunction>(
                     reinterpret_cast<void*>(GetProcAddress(kernel, "SetDefaultDllDirectories")))
               : nullptr;
    DWORD flags = LOAD_LIBRARY_SEARCH_SYSTEM32;
    if (applicationDirectory) flags |= LOAD_LIBRARY_SEARCH_APPLICATION_DIR;
    if (setDefaultDllDirectories) setDefaultDllDirectories(flags);
    // Without it (an unpatched Windows 7), the current folder at least goes.
    SetDllDirectoryW(L"");
#else
    Q_UNUSED(applicationDirectory);
#endif
}

}  // namespace rl::frame
