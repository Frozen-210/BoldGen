// Real Windows clipboard/EDIT integration in an isolated window station.
// Keyboard delivery and focus are simulated; the user's clipboard is untouched.
#include "../BoldGen/framework.h"
#include <shellapi.h>
#include <commctrl.h>
#include <ole2.h>
#include <cstdio>
#include <string>

HWND testTarget = nullptr;
HWND testEdit = nullptr;
ULONGLONG testTime = 100;
DWORD testAffinity = WDA_NONE;
BOOL WINAPI TestDisplayAffinity(HWND, DWORD affinity) { testAffinity = affinity; return TRUE; }
UINT WINAPI TestSendInput(UINT count, LPINPUT inputs, int)
{
    if (count == 4)
    {
        SendMessageW(testEdit, inputs[1].ki.wVk == 'C' ? WM_COPY : WM_PASTE, 0, 0);
    }
    return count;
}
HWND WINAPI TestForeground() { return testTarget; }
BOOL WINAPI TestFocus(DWORD, PGUITHREADINFO info) { info->hwndFocus = testEdit; return TRUE; }
SHORT WINAPI TestKeys(int) { return 0; }
ULONGLONG WINAPI TestClock() { return testTime; }

#define SendInput TestSendInput
#define GetForegroundWindow TestForeground
#define GetGUIThreadInfo TestFocus
#define GetAsyncKeyState TestKeys
#define GetTickCount64 TestClock
#define SetWindowDisplayAffinity TestDisplayAffinity
#include "../BoldGen/BoldGen.cpp"

void Require(bool success, const char* description)
{
    if (!success)
    {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu, stage %d)\n", description,
            GetLastError(), static_cast<int>(g_command.stage));
        std::exit(1);
    }
}

bool acceptSettings = false;
INT_PTR CALLBACK TestSettingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        const auto result = SettingsProc(dialog, message, wParam, lParam);
        PostMessageW(dialog, WM_APP + 20, 0, 0);
        return result;
    }
    if (message == WM_APP + 20)
    {
        Require(GetDlgItem(dialog, IDC_RUN_AT_STARTUP) && GetDlgItem(dialog, IDC_ABOUT_BUTTON) &&
            GetDlgItem(dialog, IDC_CAPTURE_BUTTON), "new Settings controls exist");
        Require(IsDlgButtonChecked(dialog, IDC_RUN_AT_STARTUP) == BST_UNCHECKED,
            "startup defaults to disabled");
        RECT client{}, button{};
        GetClientRect(dialog, &client);
        GetWindowRect(GetDlgItem(dialog, IDCANCEL), &button);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&button), 2);
        Require(button.bottom <= client.bottom && button.right <= client.right,
            "expanded footer fits inside dialog");
        CheckDlgButton(dialog, IDC_RUN_AT_STARTUP, BST_CHECKED);
        SettingsProc(dialog, WM_COMMAND, IDC_CAPTURE_BUTTON, 0);
        Require(g_pendingExcludeFromCapture && testAffinity == WDA_EXCLUDEFROMCAPTURE,
            "eye toggle applies capture exclusion");
        SettingsProc(dialog, WM_COMMAND, IDC_CAPTURE_BUTTON, 0);
        Require(!g_pendingExcludeFromCapture && testAffinity == WDA_NONE,
            "eye toggle removes capture exclusion");
        SettingsProc(dialog, WM_COMMAND, IDC_CAPTURE_BUTTON, 0);
        HWND about = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_ABOUTBOX), dialog, AboutProc, TRUE);
        Require(about && GetDlgItem(about, IDOK) && testAffinity == WDA_EXCLUDEFROMCAPTURE,
            "About dialog exists and inherits capture exclusion");
        DestroyWindow(about);
        return SettingsProc(dialog, WM_COMMAND, acceptSettings ? IDOK : IDCANCEL, 0);
    }
    return SettingsProc(dialog, message, wParam, lParam);
}

void TestSettings()
{
    // Redirect HKCU within this test process. Never change the real startup key.
    const std::wstring testKey = L"Software\\BoldGen.Tests." + std::to_wstring(GetCurrentProcessId());
    HKEY sandbox = nullptr;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, nullptr, 0,
        KEY_ALL_ACCESS, nullptr, &sandbox, nullptr) == ERROR_SUCCESS, "test registry sandbox");
    Require(RegOverridePredefKey(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS, "redirect test registry");
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);
    g_pendingHotkeys = g_hotkeys;
    g_excludeFromCapture = g_pendingExcludeFromCapture = false;
    Require(DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, TestSettingsProc, 0) == IDCANCEL,
        "Cancel closes Settings");
    LoadCapturePreference();
    Require(!StartupEnabled() && !g_excludeFromCapture, "Cancel does not persist preferences");
    g_pendingExcludeFromCapture = false;
    acceptSettings = true;
    Require(DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, TestSettingsProc, 0) == IDOK,
        "OK saves Settings");
    LoadCapturePreference();
    Require(StartupEnabled() && g_excludeFromCapture, "startup and capture preferences persist");
    wchar_t startup[260]{};
    DWORD bytes = sizeof(startup);
    Require(RegGetValueW(HKEY_CURRENT_USER, STARTUP_KEY, L"BoldGen", RRF_RT_REG_SZ,
        nullptr, startup, &bytes) == ERROR_SUCCESS && startup[0] == L'"' &&
        startup[wcslen(startup) - 1] == L'"', "startup executable path is quoted");
    Require(SetStartupEnabled(false) == ERROR_SUCCESS && !StartupEnabled(), "startup can be disabled");
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(sandbox);
    Require(RegDeleteTreeW(HKEY_CURRENT_USER, testKey.c_str()) == ERROR_SUCCESS, "remove test registry sandbox");
    std::puts("PASS: Settings controls, About, capture toggle, startup persistence, and Cancel behavior.");
}

int main()
{
    HWINSTA station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    Require(station && SetProcessWindowStation(station), "isolated window station");
    HDESK desktop = CreateDesktopW(L"BoldGenClipboardTests", nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    Require(desktop && SetThreadDesktop(desktop), "isolated desktop");
    hInst = GetModuleHandleW(nullptr);
    testTarget = CreateWindowExW(0, L"STATIC", L"Test target", WS_POPUP,
        0, 0, 320, 200, nullptr, nullptr, hInst, nullptr);
    testEdit = CreateWindowExW(0, L"EDIT", L"Ab9", WS_CHILD | ES_MULTILINE,
        0, 0, 300, 150, testTarget, nullptr, hInst, nullptr);
    g_mainWindow = CreateWindowExW(0, L"STATIC", L"Clipboard owner", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    Require(testTarget && testEdit && g_mainWindow, "test windows");

    for (int style = 1; style <= HOTKEY_COUNT; ++style)
    {
        Require(WriteClipboardText(g_mainWindow, L"original clipboard"), "seed clipboard");
        std::unique_ptr<ClipboardBackup> backup;
        DWORD sequence = 0;
        Require(BackupClipboard(g_mainWindow, backup, sequence) == ClipboardResult::Success,
            "real clipboard backup including Windows-synthesized formats");
        Require(sequence == GetClipboardSequenceNumber(), "backup sequence is current");
        SetWindowTextW(testEdit, L"Ab9");
        SendMessageW(testEdit, EM_SETSEL, 0, -1);
        g_hotkeysEnabled = true;
        g_commandBusy = true;
        HandleHotkey(style, testTarget);
        AdvanceCommand();
        AdvanceCommand();
        AdvanceCommand();
        Require(g_command.sequence == GetClipboardSequenceNumber(), "paste sequence includes synthesized formats");
        wchar_t pasted[128]{};
        GetWindowTextW(testEdit, pasted, 128);
        Require(pasted == TransformText(L"Ab9", static_cast<TextStyle>(style - 1)),
            "native EDIT receives transformed selection");
        testTime += PASTE_SETTLE_MS;
        AdvanceCommand();
        std::wstring restored;
        Require(ReadClipboardText(g_mainWindow, restored) && restored == L"original clipboard",
            "real clipboard restored after paste");
        Require(!g_commandBusy, "command completed");
    }
    TestSettings();
    DestroyWindow(testEdit);
    DestroyWindow(testTarget);
    DestroyWindow(g_mainWindow);
    // Process teardown closes the isolated desktop/station and its clipboard.
    std::puts("PASS: all 7 styles pasted and clipboard restored using real Win32 APIs.");
}
