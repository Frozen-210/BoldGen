// Exercise the production callback and command pipeline with simulated Win32 I/O.
// These tests do not install a global hook, type into apps, or touch the clipboard.
#include "../BoldGen/framework.h"
#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <vector>
#include <string>

namespace Fake
{
    bool keys[256]{};
    HWND target = reinterpret_cast<HWND>(100);
    HWND foreground = target;
    HWND focus = reinterpret_cast<HWND>(101);
    ULONGLONG now = 100;
    DWORD sequence = 1;
    HGLOBAL clipboard = nullptr;
    bool clipboardLocked = false;
    bool unicode = true;
    bool failCopy = false;
    bool failPaste = false;
    bool failPost = false;
    int action = 0;
    int posted = 0;
    int nextHook = 0;
    std::vector<WORD> combos;

    SHORT WINAPI AsyncKey(int vk) { return keys[vk] ? SHORT(0x8000) : 0; }
    HWND WINAPI Foreground() { return foreground; }
    BOOL WINAPI WindowExists(HWND hwnd) { return hwnd == target; }
    DWORD WINAPI WindowThread(HWND, LPDWORD) { return 10; }
    BOOL WINAPI GuiInfo(DWORD, PGUITHREADINFO info)
    {
        info->hwndFocus = focus;
        return TRUE;
    }
    ULONGLONG WINAPI Clock() { return now; }
    UINT_PTR WINAPI Timer(HWND, UINT_PTR id, UINT, TIMERPROC) { return id; }
    BOOL WINAPI CancelTimer(HWND, UINT_PTR) { return TRUE; }
    LRESULT WINAPI NextHook(HHOOK, int, WPARAM, LPARAM) { ++nextHook; return 0; }
    BOOL WINAPI Post(HWND, UINT, WPARAM id, LPARAM)
    {
        if (failPost) return FALSE;
        action = static_cast<int>(id);
        ++posted;
        return TRUE;
    }
    UINT WINAPI Input(UINT count, LPINPUT inputs, int)
    {
        if (count == 4)
        {
            const WORD vk = inputs[1].ki.wVk;
            combos.push_back(vk);
            if ((vk == 'C' && failCopy) || (vk == 'V' && failPaste)) return 0;
        }
        return count;
    }
    DWORD WINAPI Sequence() { return sequence; }
    BOOL WINAPI Open(HWND) { return !clipboardLocked; }
    BOOL WINAPI Close() { return TRUE; }
    BOOL WINAPI Format(UINT) { return unicode && clipboard != nullptr; }
    HANDLE WINAPI GetData(UINT) { return clipboard; }
    BOOL WINAPI Empty()
    {
        if (clipboard) GlobalFree(clipboard);
        clipboard = nullptr;
        ++sequence;
        return TRUE;
    }
    HANDLE WINAPI SetData(UINT, HANDLE data)
    {
        clipboard = data;
        unicode = true;
        ++sequence;
        return data;
    }
    void Publish(const std::wstring& text)
    {
        Empty();
        clipboard = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        void* memory = GlobalLock(clipboard);
        memcpy(memory, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(clipboard);
        unicode = true;
    }
}

#define GetAsyncKeyState Fake::AsyncKey
#define GetForegroundWindow Fake::Foreground
#define IsWindow Fake::WindowExists
#define GetWindowThreadProcessId Fake::WindowThread
#define GetGUIThreadInfo Fake::GuiInfo
#define GetTickCount64 Fake::Clock
#define SetTimer Fake::Timer
#define KillTimer Fake::CancelTimer
#define CallNextHookEx Fake::NextHook
#define PostMessageW Fake::Post
#define SendInput Fake::Input
#define GetClipboardSequenceNumber Fake::Sequence
#define OpenClipboard Fake::Open
#define CloseClipboard Fake::Close
#define IsClipboardFormatAvailable Fake::Format
#define GetClipboardData Fake::GetData
#define EmptyClipboard Fake::Empty
#define SetClipboardData Fake::SetData
#include "../BoldGen/BoldGen.cpp"

int checks = 0;
void Check(bool success, const char* description)
{
    ++checks;
    if (!success)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}

void Reset()
{
    FinishCommand();
    std::fill(std::begin(Fake::keys), std::end(Fake::keys), false);
    g_keysDown.fill(false);
    g_suppressedKeys.fill(false);
    g_suppressedKeyCount = 0;
    g_hotkeysEnabled = true;
    g_mainWindow = reinterpret_cast<HWND>(200);
    for (int i = 0; i < HOTKEY_COUNT; ++i)
        g_hotkeys[i] = { MOD_CONTROL | MOD_ALT, static_cast<UINT>('1' + i) };
    Fake::foreground = Fake::target;
    Fake::focus = reinterpret_cast<HWND>(101);
    Fake::now = 100;
    Fake::posted = 0;
    Fake::nextHook = 0;
    Fake::failPost = Fake::failCopy = Fake::failPaste = false;
    Fake::clipboardLocked = false;
    Fake::combos.clear();
    Fake::Publish(L"old clipboard");
}

LRESULT Key(UINT vk, bool down, DWORD flags = 0)
{
    KBDLLHOOKSTRUCT event{};
    event.vkCode = vk;
    event.flags = flags;
    const auto result = KeyboardHookProc(HC_ACTION,
        down ? WM_KEYDOWN : WM_KEYUP, reinterpret_cast<LPARAM>(&event));
    // Windows updates async state after the callback, only for unconsumed input.
    if (!result && !(flags & LLKHF_INJECTED)) Fake::keys[vk] = down;
    return result;
}

void Trigger()
{
    Key(VK_CONTROL, true);
    Key(VK_MENU, true);
    Check(Key('1', true) == 1, "configured shortcut is consumed");
    HandleHotkey(Fake::action, Fake::target);
}

void Release()
{
    Key('1', false);
    Key(VK_CONTROL, false);
    Key(VK_MENU, false);
}

int main()
{
    Reset();
    Trigger();
    Check(Fake::posted == 1, "one action is queued");
    Check(Key('1', true) == 1 && Fake::posted == 1, "repeat is consumed once");
    AdvanceCommand();
    Check(Fake::combos.empty(), "copy waits for held shortcut");
    Key('1', false);
    AdvanceCommand();
    Check(Fake::combos.empty(), "copy still waits for modifiers");
    Release();
    AdvanceCommand();
    Check(Fake::combos == std::vector<WORD>{'C'}, "copy after complete release");
    Fake::now += 700;
    AdvanceCommand();
    Check(g_commandBusy && Fake::combos.size() == 1, "slow copy retries past 300ms");
    Fake::Publish(L"Ab9");
    Fake::clipboardLocked = true;
    AdvanceCommand();
    Check(g_command.stage == CommandStage::Copy, "busy clipboard is retried");
    Fake::clipboardLocked = false;
    AdvanceCommand();
    AdvanceCommand();
    std::wstring result;
    Check(ReadClipboardText(g_mainWindow, result) &&
        result == L"\U0001D400\U0001D41B\U0001D7D7", "correct bold clipboard output");
    Check(Fake::combos == std::vector<WORD>{'C', 'V'} && !g_commandBusy,
        "one copy and one paste complete the command");

    Reset();
    Trigger();
    Check(Key('2', true) == 1 && Fake::posted == 1,
        "another shortcut during a command is consumed without overlapping it");
    Release();
    AdvanceCommand();
    Check(Fake::combos.empty(), "waits for all suppressed keys");
    Key('2', false);
    AdvanceCommand();
    Check(Fake::combos.size() == 1, "continues after additional key release");

    Reset();
    g_hotkeys[0] = { MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'S' };
    Key(VK_CONTROL, true); Key(VK_MENU, true); Key(VK_SHIFT, true);
    Check(Key('S', true) == 1 && Fake::action == 1, "saved Ctrl+Alt+Shift+S works");
    Check(Key('S', false, LLKHF_INJECTED) == 0 && g_suppressedKeyCount == 1,
        "injected release cannot release physical shortcut");
    Check(Key('S', false) == 1 && g_suppressedKeyCount == 0,
        "physical key-up is consumed and releases shortcut");

    Reset();
    Key(VK_CONTROL, true); Key(VK_MENU, true); Key(VK_SHIFT, true);
    Check(Key('1', true) == 0 && Fake::posted == 0, "extra modifiers do not match");
    Reset();
    Key(VK_CONTROL, true); Key(VK_MENU, true);
    Check(Key('1', true, LLKHF_INJECTED) == 0 && Fake::posted == 0,
        "injected shortcut passes through");
    g_hotkeysEnabled = false;
    Check(Key('1', true) == 0 && Fake::posted == 0, "settings allow shortcut capture");
    Check(KeyboardHookProc(-1, 0, 0) == 0, "negative hook codes pass through");
    Reset();
    Trigger();
    g_hotkeysEnabled = false;
    Check(Key('1', false) == 1 && g_suppressedKeyCount == 0,
        "opening settings still consumes the paired release");

    Reset();
    Fake::failPost = true;
    Key(VK_CONTROL, true); Key(VK_MENU, true);
    Check(Key('1', true) == 0 && !g_commandBusy && g_suppressedKeyCount == 0,
        "failed dispatch restores state and passes the key through");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::now += 2100;
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1,
        "failed copy never pastes old clipboard text");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"not text"); Fake::unicode = false;
    AdvanceCommand(); Fake::now += 2100; AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "non-text is never pasted");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand();
    Fake::foreground = nullptr;
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "changed window cancels paste");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand();
    Fake::focus = reinterpret_cast<HWND>(102);
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "changed control cancels paste");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand(); Fake::Publish(L"new copy");
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "new clipboard cancels paste");

    Reset(); Trigger(); Release(); Fake::failCopy = true; AdvanceCommand();
    Check(!g_commandBusy, "failed Ctrl+C ends the operation");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand(); Fake::failPaste = true;
    AdvanceCommand();
    Check(!g_commandBusy, "failed Ctrl+V ends the operation");

    const std::wstring plain = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 !\n\U0001F600";
    for (int i = 0; i < 6; ++i)
    {
        const auto decorated = TransformText(plain, static_cast<TextStyle>(i));
        Check(TransformText(decorated, TextStyle::Reset) == plain,
            "all letters/digits reset correctly and preserve emoji/punctuation");
        Check(TransformText(decorated, TextStyle::Code) == TransformText(plain, TextStyle::Code),
            "restyling existing decoration works");
    }
    Fake::Empty();
    std::printf("PASS: %d checks (hook handling, copy/paste, Unicode).\n", checks);
}
