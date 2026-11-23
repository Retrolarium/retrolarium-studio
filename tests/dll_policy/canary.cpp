#include <windows.h>

extern "C" DWORD WINAPI CanaryVersionSize(LPCWSTR, LPDWORD) { return 0; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[32768] = {};
        GetModuleFileNameW(module, path, 32700);
        lstrcatW(path, L".loaded");
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, "loaded", 6, &written, nullptr);
            CloseHandle(file);
        }
    }
    return TRUE;
}
