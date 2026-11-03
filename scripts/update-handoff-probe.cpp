#include <windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t path[32768], value[32768];
    if (!GetEnvironmentVariableW(L"AWAKE_RESTART_PROBE", path, 32768)) return 1;
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring result;
    for (int i = 1; i < count; ++i) { result += args[i]; result += L"\n"; }
    for (auto name : {L"QSG_RHI_PREFER_SOFTWARE_RENDERER", L"QTWEBENGINE_CHROMIUM_FLAGS", L"AWAKE_HANDOFF_SENTINEL"}) {
        DWORD length = GetEnvironmentVariableW(name, value, 32768);
        result += length ? value : L"<unset>";
        result += L"\n";
    }
    auto file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    WriteFile(file, result.data(), static_cast<DWORD>(result.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file); LocalFree(args);
    return 0;
}
