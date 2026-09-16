// Exercise the production callback and command pipeline with simulated Win32 I/O.
// These tests do not install a global hook, type into apps, or touch the clipboard.
#include "../BoldGen/framework.h"
#include <shellapi.h>
#include <commctrl.h>
#include <ole2.h>
#include <cstdio>
#include <vector>
#include <string>
#include <map>

void FreeClipboardHandle(UINT format, HANDLE data);

namespace Fake
{
    bool keys[256]{};
    HWND target = reinterpret_cast<HWND>(100);
    HWND foreground = target;
    HWND focus = reinterpret_cast<HWND>(101);
    ULONGLONG now = 100;
    DWORD sequence = 1;
    HGLOBAL clipboard = nullptr;
    std::map<UINT, HANDLE> extraFormats;
    HWND clipboardOwner = target;
    HWND openOwner = nullptr;
    bool failBackup = false;
    UINT failSetFormat = 0;
    bool clipboardLocked = false;
    bool clipboardOpen = false;
    bool pendingSynthesis = false;
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
    DWORD WINAPI WindowThread(HWND hwnd, LPDWORD process)
    {
        if (process) *process = hwnd == target ? 1000 : 2000;
        return 10;
    }
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
    BOOL WINAPI Open(HWND hwnd)
    {
        if (clipboardLocked) return FALSE;
        openOwner = hwnd;
        clipboardOpen = true;
        return TRUE;
    }
    BOOL WINAPI Close()
    {
        // Real Windows publishes synthesized text formats at CloseClipboard,
        // which changes the sequence after the last SetClipboardData call.
        if (pendingSynthesis && clipboard) sequence += 3;
        pendingSynthesis = false;
        clipboardOpen = false;
        return TRUE;
    }
    HWND WINAPI Owner() { return clipboardOwner; }
    BOOL WINAPI Format(UINT format)
    {
        return format == CF_UNICODETEXT ? unicode && clipboard != nullptr :
            extraFormats.count(format) != 0;
    }
    UINT WINAPI Formats(UINT previous)
    {
        if (!previous && clipboard) return CF_UNICODETEXT;
        const auto it = extraFormats.upper_bound(previous == CF_UNICODETEXT ? 0 : previous);
        return it != extraFormats.end() ? it->first : 0;
    }
    HANDLE WINAPI GetData(UINT format)
    {
        if (failBackup) return nullptr;
        if (format == CF_UNICODETEXT) return clipboard;
        const auto it = extraFormats.find(format);
        return it != extraFormats.end() ? it->second : nullptr;
    }
    BOOL WINAPI Empty()
    {
        if (clipboard) GlobalFree(clipboard);
        clipboard = nullptr;
        for (const auto& entry : extraFormats) FreeClipboardHandle(entry.first, entry.second);
        extraFormats.clear();
        clipboardOwner = openOwner;
        pendingSynthesis = clipboardOpen;
        ++sequence;
        return TRUE;
    }
    HANDLE WINAPI SetData(UINT format, HANDLE data)
    {
        if (format == failSetFormat) return nullptr;
        if (format == CF_UNICODETEXT)
        {
            if (clipboard) GlobalFree(clipboard);
            clipboard = data;
            unicode = true;
        }
        else
        {
            if (extraFormats.count(format)) FreeClipboardHandle(format, extraFormats[format]);
            extraFormats[format] = data;
        }
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
        clipboardOwner = target;
    }
    void AddBytes(UINT format, const char* text)
    {
        const size_t size = strlen(text) + 1;
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
        void* memory = GlobalLock(data);
        memcpy(memory, text, size);
        GlobalUnlock(data);
        SetData(format, data);
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
#define EnumClipboardFormats Fake::Formats
#define GetClipboardOwner Fake::Owner
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
    ClearCommand();
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
    Fake::clipboardOpen = Fake::pendingSynthesis = false;
    Fake::failBackup = false;
    Fake::failSetFormat = 0;
    g_exitRequested = false;
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

void PasteSelection()
{
    Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"Ab9"); AdvanceCommand(); AdvanceCommand();
}

bool ClipboardEquals(const wchar_t* expected)
{
    std::wstring text;
    return ReadClipboardText(g_mainWindow, text) && text == expected;
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
    Check(Fake::combos == std::vector<WORD>{'C', 'V'} && g_commandBusy,
        "one copy and one paste precede restoration");
    AdvanceCommand();
    Check(g_command.stage == CommandStage::Restore, "restoration waits for paste to settle");
    Fake::now += PASTE_SETTLE_MS;
    AdvanceCommand();
    Check(ReadClipboardText(g_mainWindow, result) && result == L"old clipboard" &&
        !g_commandBusy, "original clipboard restored after paste");

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
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "non-text is never pasted");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand();
    Fake::foreground = nullptr;
    AdvanceCommand();
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "changed window cancels paste");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand();
    Fake::focus = reinterpret_cast<HWND>(102);
    AdvanceCommand();
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "changed control cancels paste");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand(); Fake::Publish(L"new copy");
    AdvanceCommand();
    AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 1, "new clipboard cancels paste");

    Reset(); Trigger(); Release(); Fake::failCopy = true; AdvanceCommand();
    Fake::now += 2100; AdvanceCommand();
    Check(!g_commandBusy, "failed Ctrl+C ends the operation");
    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"hello"); AdvanceCommand(); Fake::failPaste = true;
    AdvanceCommand();
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy, "failed Ctrl+V ends the operation");
    Check(ClipboardEquals(L"old clipboard"), "failed paste restores original clipboard");

    Reset();
    constexpr UINT htmlFormat = 0xC001;
    Fake::AddBytes(htmlFormat, "<b>original rich text</b>");
    const DWORD pixels[] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00FFFFFF };
    Fake::SetData(CF_BITMAP, CreateBitmap(2, 2, 1, 32, pixels));
    PasteSelection();
    Check(Fake::extraFormats.empty(), "only transformed text is exposed for paste");
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(ClipboardEquals(L"old clipboard"), "multi-format backup restores plain text");
    const auto html = static_cast<const char*>(GlobalLock(Fake::GetData(htmlFormat)));
    Check(html && strcmp(html, "<b>original rich text</b>") == 0,
        "registered rich-text format restored byte-for-byte");
    GlobalUnlock(Fake::GetData(htmlFormat));
    DWORD restoredPixels[4]{};
    Check(GetBitmapBits(static_cast<HBITMAP>(Fake::GetData(CF_BITMAP)),
        sizeof(restoredPixels), restoredPixels) == sizeof(restoredPixels) &&
        memcmp(pixels, restoredPixels, sizeof(pixels)) == 0, "bitmap handles are deep-copied");

    Reset(); Fake::Empty(); PasteSelection();
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!Fake::clipboard && Fake::extraFormats.empty() && !g_commandBusy,
        "originally empty clipboard is restored to empty");

    Reset(); Trigger(); Release(); Fake::clipboardLocked = true; AdvanceCommand();
    Check(Fake::combos.empty() && !g_command.backup, "copy waits for a complete backup");
    Fake::clipboardLocked = false; AdvanceCommand();
    Check(g_command.backup != nullptr && Fake::combos.size() == 1,
        "copy begins only after backup succeeds");

    Reset(); Fake::failBackup = true; Trigger(); Release(); AdvanceCommand();
    Check(Fake::combos.empty() && !g_commandBusy, "unreadable original clipboard cancels copy");
    Fake::failBackup = false;
    Check(ClipboardEquals(L"old clipboard"), "failed backup preserves original content");
    Reset(); Fake::AddBytes(CF_OWNERDISPLAY, "owner-dependent data");
    Trigger(); Release(); AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.empty() && ClipboardEquals(L"old clipboard"),
        "unsupported owner-dependent format cancels without clipboard loss");
    // Fake owner-display storage is only test memory; Windows does not free it.
    GlobalFree(Fake::extraFormats[CF_OWNERDISPLAY]);
    Fake::extraFormats.erase(CF_OWNERDISPLAY);

    Reset(); PasteSelection(); Fake::clipboardLocked = true;
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(g_commandBusy, "clipboard lock delays restoration");
    Fake::clipboardLocked = false; AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"old clipboard"), "locked restoration is retried");

    Reset(); PasteSelection(); Fake::Publish(L"new user clipboard");
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"new user clipboard"),
        "restoration never overwrites a newer clipboard change");

    Reset(); Fake::AddBytes(htmlFormat, "rich backup"); PasteSelection();
    Fake::failSetFormat = htmlFormat; Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(g_commandBusy && g_command.backup->entries.size() == 2,
        "partial restore retains a complete backup for retry");
    Fake::failSetFormat = 0; AdvanceCommand();
    Check(!g_commandBusy && Fake::Format(htmlFormat) && ClipboardEquals(L"old clipboard"),
        "partial restore retry recovers every original format");

    Reset(); Trigger(); Release(); AdvanceCommand(); Fake::Publish(L"selection");
    AdvanceCommand(); Fake::failSetFormat = CF_UNICODETEXT; AdvanceCommand();
    Check(g_command.stage == CommandStage::Restore && Fake::combos.size() == 1,
        "failed transformed write initiates restoration without pasting");
    Fake::failSetFormat = 0; AdvanceCommand();
    Check(ClipboardEquals(L"old clipboard") && !g_commandBusy,
        "clipboard restored even after transformed write emptied it");

    Reset(); PasteSelection(); g_hotkeysEnabled = false; FinishCommand();
    AdvanceCommand();
    Check(g_commandBusy && g_command.stage == CommandStage::Restore,
        "settings cannot prematurely restore an in-flight paste");
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"old clipboard"),
        "restoration continues while settings are open");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::foreground = nullptr; AdvanceCommand();
    Check(g_command.cancellingCopy && g_commandBusy, "cancel waits for an in-flight copy");
    Fake::Publish(L"late selection"); AdvanceCommand(); AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"old clipboard") && Fake::combos.size() == 1,
        "late copy is restored after cancellation without paste");

    Reset(); PasteSelection(); RequestExit();
    Check(g_exitRequested && g_commandBusy, "exit waits for pending restoration");
    Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"old clipboard") && Fake::posted == 2,
        "exit is queued after original clipboard is restored");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"selection from a clipboard helper");
    Fake::clipboardOwner = reinterpret_cast<HWND>(300); AdvanceCommand();
    Check(g_command.stage == CommandStage::Paste,
        "fresh copy from a helper process is accepted");
    AdvanceCommand(); Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy && Fake::combos.size() == 2 && ClipboardEquals(L"old clipboard"),
        "helper-process copy is transformed, pasted, and original clipboard restored");

    Reset(); Trigger(); Release(); AdvanceCommand();
    Fake::Publish(L"selection whose owner window has closed");
    Fake::clipboardOwner = nullptr; AdvanceCommand();
    Check(g_command.stage == CommandStage::Paste, "valid ownerless clipboard text is accepted");
    AdvanceCommand(); Fake::now += PASTE_SETTLE_MS; AdvanceCommand();
    Check(!g_commandBusy && ClipboardEquals(L"old clipboard"),
        "ownerless copy completes with restoration");

    Reset(); PasteSelection(); Fake::clipboardLocked = true;
    Fake::now += PASTE_SETTLE_MS + RESTORE_TIMEOUT_MS; AdvanceCommand();
    Check(!g_commandBusy, "restoration timeout releases command state");
    Fake::clipboardLocked = false;

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
    std::printf("PASS: %d checks (hook handling, copy/paste/restoration, Unicode).\n", checks);
}
