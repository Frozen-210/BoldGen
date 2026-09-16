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
    DestroyWindow(testEdit);
    DestroyWindow(testTarget);
    DestroyWindow(g_mainWindow);
    // Process teardown closes the isolated desktop/station and its clipboard.
    std::puts("PASS: all 7 styles pasted and clipboard restored using real Win32 APIs.");
}
