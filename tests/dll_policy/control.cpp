#include <windows.h>
#include <winver.h>

int main() {
    // A real static VERSION.dll import, resolved before main(). The canary
    // implements it too; the return value deliberately does not decide pass.
    DWORD ignored = 0;
    GetFileVersionInfoSizeW(L"retrolarium-dll-policy-probe-not-present", &ignored);
    return 0;
}
