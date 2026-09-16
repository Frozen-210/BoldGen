#include "framework.h"
#include "BoldGen.h"

#include <shellapi.h>
#include <commctrl.h>
#include <ole2.h>

#include <array>
#include <string>
#include <algorithm>
#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")

#define MAX_LOADSTRING 100

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_RUN_HOTKEY = WM_APP + 2;
constexpr UINT_PTR COMMAND_TIMER = 1;
constexpr ULONGLONG PASTE_SETTLE_MS = 500;
constexpr ULONGLONG RESTORE_TIMEOUT_MS = 2000;
constexpr UINT TRAY_ICON_ID = 1;

constexpr UINT ID_TRAY_SETTINGS = 50001;
constexpr UINT ID_TRAY_EXIT = 50002;

constexpr int HOTKEY_COUNT = 7;

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

HWND g_mainWindow = nullptr;

UINT g_taskbarCreated = 0;

struct Hotkey
{
    UINT modifiers;
    UINT vk;
};

// Default hotkeys
std::array<Hotkey, HOTKEY_COUNT> g_hotkeys =
{ {
    { MOD_CONTROL | MOD_ALT, '1' }, // Bold Serif
    { MOD_CONTROL | MOD_ALT, '2' }, // Bold Sans
    { MOD_CONTROL | MOD_ALT, '3' }, // Bold Italic Serif
    { MOD_CONTROL | MOD_ALT, '4' }, // Bold Italic Sans
    { MOD_CONTROL | MOD_ALT, '5' }, // Double Struck
    { MOD_CONTROL | MOD_ALT, '6' }, // Code
    { MOD_CONTROL | MOD_ALT, '7' }, // Reset
} };

std::array<Hotkey, HOTKEY_COUNT> g_pendingHotkeys;

// The hook has its own message loop. Clipboard work never runs on that thread.
HANDLE g_hookThread = nullptr;
DWORD g_hookThreadId = 0;
HANDLE g_hookReady = nullptr;
DWORD g_hookError = ERROR_SUCCESS;
SRWLOCK g_hotkeyLock = SRWLOCK_INIT;
std::atomic<bool> g_hotkeysEnabled{ false };
std::atomic<bool> g_commandBusy{ false };
std::atomic<UINT> g_suppressedKeyCount{ 0 };
// Accessed only by the hook thread.
std::array<bool, 256> g_keysDown{};
std::array<bool, 256> g_suppressedKeys{};

constexpr int g_hotkeyControlIds[HOTKEY_COUNT] =
{
    IDC_HK_BOLD_SERIF,
    IDC_HK_BOLD_SANS,
    IDC_HK_BOLD_ITALIC_SERIF,
    IDC_HK_BOLD_ITALIC_SANS,
    IDC_HK_DOUBLE_STRUCK,
    IDC_HK_CODE,
    IDC_HK_RESET
};

enum class TextStyle
{
    BoldSerif = 0,
    BoldSans,
    BoldItalicSerif,
    BoldItalicSans,
    DoubleStruck,
    Code,
    Reset
};

// ------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);

bool AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);
void ShowTrayMenu(HWND hwnd);

bool StartKeyboardHook();
void StopKeyboardHook();
bool IsModifierKey(UINT vk);

void ShowSettings(HWND owner);

void HandleHotkey(int id, HWND target);
void AdvanceCommand();
void FinishCommand(const wchar_t* reason = nullptr, bool notify = false);
void RequestExit();

bool SendCtrlCombo(WORD key);

bool ReadClipboardText(HWND hwnd, std::wstring& text, DWORD* sequence = nullptr);
bool WriteClipboardText(HWND hwnd, const std::wstring& text,
    const DWORD* expectedSequence = nullptr, DWORD* writtenSequence = nullptr);

std::wstring TransformText(
    const std::wstring& text,
    TextStyle style);

// ------------------------------------------------------------
// Unicode helpers
// ------------------------------------------------------------

void AppendCodePoint(std::wstring& output, uint32_t cp)
{
    if (cp <= 0xFFFF)
    {
        output.push_back(static_cast<wchar_t>(cp));
        return;
    }

    cp -= 0x10000;

    wchar_t high =
        static_cast<wchar_t>(0xD800 + ((cp >> 10) & 0x3FF));

    wchar_t low =
        static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));

    output.push_back(high);
    output.push_back(low);
}

uint32_t ReadCodePoint(
    const std::wstring& text,
    size_t& index)
{
    uint32_t first =
        static_cast<uint16_t>(text[index++]);

    if (first >= 0xD800 &&
        first <= 0xDBFF &&
        index < text.size())
    {
        uint32_t second =
            static_cast<uint16_t>(text[index]);

        if (second >= 0xDC00 &&
            second <= 0xDFFF)
        {
            ++index;

            return
                0x10000 +
                ((first - 0xD800) << 10) +
                (second - 0xDC00);
        }
    }

    return first;
}

// ------------------------------------------------------------
// Unicode mathematical mappings
// ------------------------------------------------------------

uint32_t MapAsciiToStyle(
    uint32_t ch,
    TextStyle style)
{
    switch (style)
    {
    case TextStyle::BoldSerif:
        if (ch >= 'A' && ch <= 'Z')
            return 0x1D400 + (ch - 'A');

        if (ch >= 'a' && ch <= 'z')
            return 0x1D41A + (ch - 'a');

        if (ch >= '0' && ch <= '9')
            return 0x1D7CE + (ch - '0');

        break;

    case TextStyle::BoldSans:
        if (ch >= 'A' && ch <= 'Z')
            return 0x1D5D4 + (ch - 'A');

        if (ch >= 'a' && ch <= 'z')
            return 0x1D5EE + (ch - 'a');

        if (ch >= '0' && ch <= '9')
            return 0x1D7EC + (ch - '0');

        break;

    case TextStyle::BoldItalicSerif:
        if (ch >= 'A' && ch <= 'Z')
            return 0x1D468 + (ch - 'A');

        if (ch >= 'a' && ch <= 'z')
            return 0x1D482 + (ch - 'a');

        break;

    case TextStyle::BoldItalicSans:
        if (ch >= 'A' && ch <= 'Z')
            return 0x1D63C + (ch - 'A');

        if (ch >= 'a' && ch <= 'z')
            return 0x1D656 + (ch - 'a');

        break;

    case TextStyle::DoubleStruck:
    {
        static constexpr uint32_t upper[26] =
        {
            0x1D538, // A
            0x1D539, // B
            0x2102,  // C
            0x1D53B, // D
            0x1D53C, // E
            0x1D53D, // F
            0x1D53E, // G
            0x210D,  // H
            0x1D540, // I
            0x1D541, // J
            0x1D542, // K
            0x1D543, // L
            0x1D544, // M
            0x2115,  // N
            0x1D546, // O
            0x2119,  // P
            0x211A,  // Q
            0x211D,  // R
            0x1D54A, // S
            0x1D54B, // T
            0x1D54C, // U
            0x1D54D, // V
            0x1D54E, // W
            0x1D54F, // X
            0x1D550, // Y
            0x2124   // Z
        };

        if (ch >= 'A' && ch <= 'Z')
            return upper[ch - 'A'];

        if (ch >= 'a' && ch <= 'z')
            return 0x1D552 + (ch - 'a');

        if (ch >= '0' && ch <= '9')
            return 0x1D7D8 + (ch - '0');

        break;
    }

    case TextStyle::Code:
        if (ch >= 'A' && ch <= 'Z')
            return 0x1D670 + (ch - 'A');

        if (ch >= 'a' && ch <= 'z')
            return 0x1D68A + (ch - 'a');

        if (ch >= '0' && ch <= '9')
            return 0x1D7F6 + (ch - '0');

        break;

    case TextStyle::Reset:
        break;
    }

    return ch;
}

uint32_t StyledToAscii(uint32_t cp)
{
    // Plain ASCII already stays plain.
    if (cp <= 0x7F)
        return cp;

    constexpr TextStyle styles[] =
    {
        TextStyle::BoldSerif,
        TextStyle::BoldSans,
        TextStyle::BoldItalicSerif,
        TextStyle::BoldItalicSans,
        TextStyle::DoubleStruck,
        TextStyle::Code
    };

    for (uint32_t ch = 'A'; ch <= 'Z'; ++ch)
    {
        for (TextStyle style : styles)
        {
            if (MapAsciiToStyle(ch, style) == cp)
                return ch;
        }
    }

    for (uint32_t ch = 'a'; ch <= 'z'; ++ch)
    {
        for (TextStyle style : styles)
        {
            if (MapAsciiToStyle(ch, style) == cp)
                return ch;
        }
    }

    for (uint32_t ch = '0'; ch <= '9'; ++ch)
    {
        for (TextStyle style : styles)
        {
            uint32_t styled =
                MapAsciiToStyle(ch, style);

            if (styled != ch && styled == cp)
                return ch;
        }
    }

    return cp;
}

std::wstring TransformText(
    const std::wstring& text,
    TextStyle style)
{
    std::wstring output;
    output.reserve(text.size() * 2);

    size_t index = 0;

    while (index < text.size())
    {
        uint32_t cp =
            ReadCodePoint(text, index);

        uint32_t plain =
            StyledToAscii(cp);

        if (style == TextStyle::Reset)
        {
            AppendCodePoint(output, plain);
            continue;
        }

        // This also lets you convert:
        //
        // 𝐁𝐨𝐥𝐝 -> 𝗕𝗼𝗹𝗱
        //
        // instead of requiring Reset first.
        if (plain <= 0x7F)
        {
            uint32_t decorated =
                MapAsciiToStyle(plain, style);

            AppendCodePoint(output, decorated);
        }
        else
        {
            AppendCodePoint(output, cp);
        }
    }

    return output;
}

// ------------------------------------------------------------
// Registry persistence
// ------------------------------------------------------------

void LoadHotkeys()
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\BoldGen",
        0,
        KEY_READ,
        &key) != ERROR_SUCCESS)
    {
        return;
    }

    std::array<Hotkey, HOTKEY_COUNT> temp;

    DWORD type = 0;
    DWORD size = sizeof(temp);

    LONG result =
        RegQueryValueExW(
            key,
            L"Hotkeys",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(temp.data()),
            &size);

    RegCloseKey(key);

    if (result != ERROR_SUCCESS ||
        type != REG_BINARY ||
        size != sizeof(temp))
    {
        return;
    }

    bool valid =
        std::all_of(
            temp.begin(),
            temp.end(),
            [](const Hotkey& hk)
            {
                return hk.vk != 0 && hk.vk < 256 && !IsModifierKey(hk.vk) &&
                    (hk.modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT)) == 0;
            });

    if (valid)
        g_hotkeys = temp;
}

void SaveHotkeys()
{
    HKEY key = nullptr;

    if (RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\BoldGen",
        0,
        nullptr,
        0,
        KEY_WRITE,
        nullptr,
        &key,
        nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    RegSetValueExW(
        key,
        L"Hotkeys",
        0,
        REG_BINARY,
        reinterpret_cast<const BYTE*>(g_hotkeys.data()),
        sizeof(g_hotkeys));

    RegCloseKey(key);
}

// ------------------------------------------------------------
// Keyboard input
// ------------------------------------------------------------

bool SendCtrlCombo(WORD key)
{
    INPUT input[4]{};

    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;

    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = key;

    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = key;
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;

    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_CONTROL;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(4, input, sizeof(INPUT));
    if (sent != 4)
    {
        // A partial send must not leave our synthetic Ctrl/key held down.
        SendInput(2, &input[2], sizeof(INPUT));
        return false;
    }
    return true;
}

// ------------------------------------------------------------
// Clipboard
// ------------------------------------------------------------

UINT ClipboardHandleFormat(UINT format)
{
    switch (format)
    {
    case CF_DSPBITMAP: return CF_BITMAP;
    case CF_DSPMETAFILEPICT: return CF_METAFILEPICT;
    case CF_DSPENHMETAFILE: return CF_ENHMETAFILE;
    default: return format;
    }
}

HANDLE CloneClipboardHandle(UINT format, HANDLE data)
{
    format = ClipboardHandleFormat(format);
    if (format == CF_ENHMETAFILE)
        return CopyEnhMetaFileW(static_cast<HENHMETAFILE>(data), nullptr);
    return OleDuplicateData(data, static_cast<CLIPFORMAT>(format), GMEM_MOVEABLE);
}

void FreeClipboardHandle(UINT format, HANDLE data)
{
    if (!data)
        return;
    switch (ClipboardHandleFormat(format))
    {
    case CF_BITMAP:
    case CF_PALETTE:
        DeleteObject(data);
        break;
    case CF_ENHMETAFILE:
        DeleteEnhMetaFile(static_cast<HENHMETAFILE>(data));
        break;
    case CF_METAFILEPICT:
        if (const auto picture = static_cast<METAFILEPICT*>(GlobalLock(data)))
        {
            DeleteMetaFile(picture->hMF);
            GlobalUnlock(data);
        }
        GlobalFree(data);
        break;
    default:
        GlobalFree(data);
        break;
    }
}

struct ClipboardBackup
{
    struct Entry { UINT format; HANDLE data; };
    std::vector<Entry> entries;

    ClipboardBackup() = default;
    ClipboardBackup(const ClipboardBackup&) = delete;
    ClipboardBackup& operator=(const ClipboardBackup&) = delete;
    ~ClipboardBackup()
    {
        for (const auto& entry : entries)
            FreeClipboardHandle(entry.format, entry.data);
    }
};

enum class ClipboardResult { Success, Retry, Unsupported, Changed };

ClipboardResult BackupClipboard(HWND hwnd, std::unique_ptr<ClipboardBackup>& backup,
    DWORD& sequence)
{
    if (!OpenClipboard(hwnd))
        return ClipboardResult::Retry;

    auto snapshot = std::make_unique<ClipboardBackup>();
    ClipboardResult result = ClipboardResult::Success;
    UINT format = 0;
    for (;;)
    {
        SetLastError(ERROR_SUCCESS);
        format = EnumClipboardFormats(format);
        if (format == 0)
        {
            if (GetLastError() != ERROR_SUCCESS)
                result = ClipboardResult::Unsupported;
            break;
        }
        // Owner-display and private GDI formats require the original application's
        // callbacks/ownership. Refuse the command rather than silently lose them.
        if (format == CF_OWNERDISPLAY ||
            (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST) ||
            (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST))
        {
            result = ClipboardResult::Unsupported;
            break;
        }
        const HANDLE source = GetClipboardData(format); // Materialize delayed data.
        const HANDLE copy = source ? CloneClipboardHandle(format, source) : nullptr;
        if (!copy)
        {
            result = ClipboardResult::Unsupported;
            break;
        }
        snapshot->entries.push_back({ format, copy });
    }
    sequence = GetClipboardSequenceNumber();
    CloseClipboard();
    if (result == ClipboardResult::Success && GetClipboardSequenceNumber() != sequence)
        result = ClipboardResult::Retry;
    if (result == ClipboardResult::Success)
        backup = std::move(snapshot); // An empty clipboard is also a valid backup.
    return result;
}

ClipboardResult RestoreClipboard(HWND hwnd, const ClipboardBackup& backup,
    DWORD& expectedSequence)
{
    if (!OpenClipboard(hwnd))
        return ClipboardResult::Retry;
    if (GetClipboardSequenceNumber() != expectedSequence)
    {
        CloseClipboard();
        return ClipboardResult::Changed;
    }

    // Retain the original backup until every format has been restored, so a
    // partial SetClipboardData failure can be retried without losing formats.
    ClipboardBackup copies;
    for (const auto& entry : backup.entries)
    {
        HANDLE data = CloneClipboardHandle(entry.format, entry.data);
        if (!data)
        {
            CloseClipboard();
            return ClipboardResult::Retry;
        }
        copies.entries.push_back({ entry.format, data });
    }
    if (!EmptyClipboard())
    {
        CloseClipboard();
        return ClipboardResult::Retry;
    }
    ClipboardResult result = ClipboardResult::Success;
    for (auto& entry : copies.entries)
    {
        if (!SetClipboardData(entry.format, entry.data))
        {
            result = ClipboardResult::Retry;
            break;
        }
        entry.data = nullptr; // Ownership transferred to Windows.
    }
    CloseClipboard();
    // Windows may add synthesized text formats when the clipboard is closed.
    // Capture the published sequence, not the intermediate open-clipboard value.
    const DWORD publishedSequence = GetClipboardSequenceNumber();
    if (GetClipboardOwner() != hwnd)
        return ClipboardResult::Changed;
    expectedSequence = publishedSequence;
    return result;
}

bool ReadClipboardText(
    HWND hwnd,
    std::wstring& text,
    DWORD* sequence)
{
    if (!OpenClipboard(hwnd))
        return false;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        CloseClipboard();
        return false;
    }

    HANDLE data =
        GetClipboardData(CF_UNICODETEXT);

    if (!data)
    {
        CloseClipboard();
        return false;
    }

    const wchar_t* ptr =
        static_cast<const wchar_t*>(
            GlobalLock(data));

    if (!ptr)
    {
        CloseClipboard();
        return false;
    }

    const size_t capacity = GlobalSize(data) / sizeof(wchar_t);
    const wchar_t* end = std::find(ptr, ptr + capacity, L'\0');
    const bool valid = end != ptr + capacity;
    if (valid)
        text.assign(ptr, end);
    const HWND owner = GetClipboardOwner();
    GlobalUnlock(data);
    CloseClipboard();
    const DWORD publishedSequence = GetClipboardSequenceNumber();
    if (GetClipboardOwner() != owner)
        return false;
    if (sequence)
        *sequence = publishedSequence;

    return valid;
}

bool WriteClipboardText(
    HWND hwnd,
    const std::wstring& text,
    const DWORD* expectedSequence,
    DWORD* writtenSequence)
{
    SIZE_T bytes =
        (text.size() + 1) *
        sizeof(wchar_t);

    HGLOBAL memory =
        GlobalAlloc(
            GMEM_MOVEABLE,
            bytes);

    if (!memory)
    {
        return false;
    }

    void* ptr =
        GlobalLock(memory);

    if (!ptr)
    {
        GlobalFree(memory);
        return false;
    }

    memcpy(
        ptr,
        text.c_str(),
        bytes);

    GlobalUnlock(memory);

    // Allocate before clearing the clipboard so allocation failures preserve it.
    if (!OpenClipboard(hwnd))
    {
        GlobalFree(memory);
        return false;
    }
    if (expectedSequence && GetClipboardSequenceNumber() != *expectedSequence)
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    if (!EmptyClipboard())
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    const bool success = SetClipboardData(
        CF_UNICODETEXT,
        memory) != nullptr;
    if (!success)
        GlobalFree(memory);

    // Clipboard owns memory after successful SetClipboardData.
    CloseClipboard();
    const DWORD publishedSequence = GetClipboardSequenceNumber();
    if (GetClipboardOwner() != hwnd)
        return false;
    if (writtenSequence)
        *writtenSequence = publishedSequence;
    return success;
}

// ------------------------------------------------------------
// Actual hotkey command
// ------------------------------------------------------------

enum class CommandStage { Idle, ReleaseKeys, Copy, Paste, Restore };
struct Command
{
    CommandStage stage = CommandStage::Idle;
    TextStyle style = TextStyle::Reset;
    HWND target = nullptr;
    DWORD targetThread = 0;
    HWND focus = nullptr;
    DWORD sequence = 0;
    ULONGLONG deadline = 0;
    ULONGLONG restoreAfter = 0;
    bool restoreNeeded = false;
    bool cancellingCopy = false;
    std::unique_ptr<ClipboardBackup> backup;
    std::wstring text;
} g_command;
bool g_exitRequested = false;

bool IsKeyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

UINT CurrentModifiers()
{
    UINT modifiers = 0;
    if (IsKeyDown(VK_CONTROL)) modifiers |= MOD_CONTROL;
    if (IsKeyDown(VK_MENU)) modifiers |= MOD_ALT;
    if (IsKeyDown(VK_SHIFT)) modifiers |= MOD_SHIFT;
    if (IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN)) modifiers |= MOD_WIN;
    return modifiers;
}

void ClearCommand()
{
    KillTimer(g_mainWindow, COMMAND_TIMER);
    g_command = {};
    g_commandBusy = false;
    if (g_exitRequested)
        PostMessageW(g_mainWindow, WM_CLOSE, 0, 0);
}

void FinishCommand(const wchar_t* reason, bool notify)
{
    if (reason)
    {
        OutputDebugStringW(L"BoldGen: ");
        OutputDebugStringW(reason);
        OutputDebugStringW(L"\n");
        if (notify)
        {
            NOTIFYICONDATAW info{};
            info.cbSize = sizeof(info);
            info.hWnd = g_mainWindow;
            info.uID = TRAY_ICON_ID;
            info.uFlags = NIF_INFO;
            info.dwInfoFlags = NIIF_WARNING;
            wcscpy_s(info.szInfoTitle, L"BoldGen");
            wcsncpy_s(info.szInfo, reason, _TRUNCATE);
            Shell_NotifyIconW(NIM_MODIFY, &info);
        }
    }
    if (g_command.stage == CommandStage::Restore)
        return; // Settings/Exit must not shorten the paste grace period.

    // Also recognize a completed copy when cancellation happens before the next
    // timer tick. The clipboard publisher may be a helper process, or may have
    // already closed its owner window; its PID is not a reliable copy gate.
    if (g_command.stage == CommandStage::Copy && !g_command.restoreNeeded &&
        GetClipboardSequenceNumber() != g_command.sequence)
    {
        g_command.restoreNeeded = true;
        g_command.sequence = GetClipboardSequenceNumber();
    }
    if (g_command.stage == CommandStage::Copy && !g_command.restoreNeeded &&
        GetClipboardSequenceNumber() == g_command.sequence &&
        GetTickCount64() < g_command.deadline)
    {
        // Copy is asynchronous too. If Settings/Exit/focus cancellation happens
        // before the target publishes its selection, wait for it and restore,
        // without transforming or pasting anything.
        g_command.cancellingCopy = true;
        return;
    }
    if (g_command.backup && g_command.restoreNeeded)
    {
        g_command.stage = CommandStage::Restore;
        g_command.deadline = (std::max)(GetTickCount64(), g_command.restoreAfter) +
            RESTORE_TIMEOUT_MS;
        // Keep the command timer and busy flag alive until restoration finishes.
        return;
    }
    ClearCommand();
}

bool TargetUnchanged()
{
    GUITHREADINFO info{ sizeof(info) };
    return IsWindow(g_command.target) &&
        GetForegroundWindow() == g_command.target &&
        GetGUIThreadInfo(g_command.targetThread, &info) &&
        info.hwndFocus == g_command.focus;
}

void HandleHotkey(int id, HWND target)
{
    if (!g_hotkeysEnabled || id < 1 || id > HOTKEY_COUNT || !target ||
        target == g_mainWindow || GetForegroundWindow() != target)
    {
        FinishCommand(L"Shortcut cancelled: focus changed or settings are open.");
        return;
    }

    g_command.targetThread = GetWindowThreadProcessId(target, nullptr);
    GUITHREADINFO info{ sizeof(info) };
    if (!GetGUIThreadInfo(g_command.targetThread, &info))
    {
        FinishCommand(L"Cannot determine the focused control.");
        return;
    }
    g_command.focus = info.hwndFocus;
    g_command.target = target;
    g_command.style = static_cast<TextStyle>(id - 1);
    g_command.stage = CommandStage::ReleaseKeys;
    g_command.deadline = GetTickCount64() + 5000;
    if (!SetTimer(g_mainWindow, COMMAND_TIMER, 15, nullptr))
        FinishCommand(L"Cannot start the command timer.");
}

void AdvanceCommand()
{
    if (g_command.stage == CommandStage::Idle)
        return;
    if (g_command.stage == CommandStage::Restore)
    {
        if (GetTickCount64() < g_command.restoreAfter)
            return;
        const auto result = RestoreClipboard(g_mainWindow, *g_command.backup,
            g_command.sequence);
        if (result == ClipboardResult::Retry && GetTickCount64() < g_command.deadline)
            return;
        if (result == ClipboardResult::Retry)
            OutputDebugStringW(L"BoldGen: Clipboard restoration timed out.\n");
        else if (result == ClipboardResult::Changed)
            OutputDebugStringW(L"BoldGen: New clipboard content preserved; restoration skipped.\n");
        ClearCommand();
        return;
    }
    if (g_command.cancellingCopy)
    {
        if (GetClipboardSequenceNumber() != g_command.sequence)
            FinishCommand();
        else if (GetTickCount64() >= g_command.deadline)
            ClearCommand();
        return;
    }
    if (!g_hotkeysEnabled || !TargetUnchanged())
    {
        FinishCommand(L"Command cancelled: focus changed.");
        return;
    }
    if (GetTickCount64() >= g_command.deadline)
    {
        FinishCommand(L"Timed out waiting for key release or copied text. Select text and release all shortcut keys.", true);
        return;
    }

    switch (g_command.stage)
    {
    case CommandStage::ReleaseKeys:
    {
        // Suppressed keys may never enter Windows' async key state. The hook
        // tracks the triggering key explicitly, including its swallowed key-up.
        if (g_suppressedKeyCount != 0 || CurrentModifiers() != 0 || IsKeyDown('C'))
            return;
        const auto result = BackupClipboard(g_mainWindow, g_command.backup,
            g_command.sequence);
        if (result == ClipboardResult::Retry)
            return;
        if (result != ClipboardResult::Success)
        {
            FinishCommand(L"The current clipboard could not be backed up. The selection was left unchanged.", true);
            return;
        }
        if (!TargetUnchanged() || CurrentModifiers() != 0)
        {
            FinishCommand(L"Focus or modifiers changed during clipboard backup.");
            return;
        }
        g_command.stage = CommandStage::Copy;
        g_command.deadline = GetTickCount64() + 2000;
        if (!SendCtrlCombo('C'))
        {
            FinishCommand(L"Ctrl+C could not be sent. Check whether the target application is running as administrator.", true);
            return;
        }
        break;
    }

    case CommandStage::Copy:
    {
        const DWORD sequence = GetClipboardSequenceNumber();
        if (!g_command.restoreNeeded && sequence == g_command.sequence)
            return; // Never transform stale clipboard contents after a failed copy.
        g_command.sequence = sequence;
        g_command.restoreNeeded = true;
        std::wstring source;
        DWORD copiedSequence = 0;
        if (!ReadClipboardText(g_mainWindow, source, &copiedSequence))
            return; // Retry while the copying application publishes text.
        g_command.sequence = copiedSequence;
        if (source.empty())
        {
            FinishCommand();
            return;
        }
        g_command.text = TransformText(source, g_command.style);
        g_command.stage = CommandStage::Paste;
        break;
    }

    case CommandStage::Paste:
    {
        if (GetClipboardSequenceNumber() != g_command.sequence)
        {
            FinishCommand(L"Clipboard changed again; paste cancelled.");
            return;
        }
        if (g_suppressedKeyCount != 0 || CurrentModifiers() != 0 || IsKeyDown('V'))
            return;
        DWORD writtenSequence = g_command.sequence;
        const bool written = WriteClipboardText(g_mainWindow, g_command.text,
            &g_command.sequence, &writtenSequence);
        if (!written)
        {
            if (writtenSequence != g_command.sequence)
            {
                g_command.sequence = writtenSequence;
                FinishCommand(L"Writing transformed text failed; restoring clipboard.");
            }
            return;
        }
        g_command.sequence = writtenSequence;
        // Clipboard access can trigger delayed rendering; recheck focus after it.
        if (!TargetUnchanged() || CurrentModifiers() != 0)
        {
            FinishCommand(L"Focus or modifiers changed before paste.");
            return;
        }
        // SendInput queues the paste; it does not acknowledge that the target has
        // consumed it. Keep transformed text available briefly, even on partial
        // input failure, before restoring the previous clipboard contents.
        g_command.restoreAfter = GetTickCount64() + PASTE_SETTLE_MS;
        if (!SendCtrlCombo('V'))
            FinishCommand(L"Ctrl+V could not be sent. Check whether the target application is running as administrator.", true);
        else
            FinishCommand();
        break;
    }

    case CommandStage::Restore:
    case CommandStage::Idle:
        break;
    }
}

void RequestExit()
{
    g_hotkeysEnabled = false;
    g_exitRequested = true;
    FinishCommand();
    if (g_command.stage == CommandStage::Idle)
    {
        g_exitRequested = false;
        DestroyWindow(g_mainWindow);
    }
}

// ------------------------------------------------------------
// Global hotkeys
// ------------------------------------------------------------

bool IsModifierKey(UINT vk)
{
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LWIN || vk == VK_RWIN;
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM message, LPARAM param)
{
    if (code != HC_ACTION)
        return CallNextHookEx(nullptr, code, message, param);

    const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(param);
    // In particular, our own Ctrl+C/Ctrl+V must pass through without recursion.
    if ((key.flags & LLKHF_INJECTED) || key.vkCode >= g_keysDown.size())
        return CallNextHookEx(nullptr, code, message, param);

    const UINT vk = key.vkCode;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up)
        return CallNextHookEx(nullptr, code, message, param);

    const bool repeat = g_keysDown[vk];
    g_keysDown[vk] = down;
    if (g_suppressedKeys[vk])
    {
        if (up)
        {
            g_suppressedKeys[vk] = false;
            --g_suppressedKeyCount;
        }
        return 1; // Swallow repeats and the matching key-up as well.
    }

    if (down && !repeat && !IsModifierKey(vk) && g_hotkeysEnabled)
    {
        // This event is a non-modifier, so previously pressed modifiers already
        // have their updated async state (the current key itself does not yet).
        const UINT modifiers = CurrentModifiers();
        int id = 0;
        AcquireSRWLockShared(&g_hotkeyLock);
        for (int i = 0; i < HOTKEY_COUNT; ++i)
        {
            if (g_hotkeys[i].vk == vk && g_hotkeys[i].modifiers == modifiers)
            {
                id = i + 1;
                break;
            }
        }
        ReleaseSRWLockShared(&g_hotkeyLock);

        bool expected = false;
        if (id != 0)
        {
            g_suppressedKeys[vk] = true;
            ++g_suppressedKeyCount;
            // Consume other configured shortcuts while a command is in progress,
            // without overlapping copy/paste operations or firing another app.
            if (!g_commandBusy.compare_exchange_strong(expected, true))
                return 1;
            if (PostMessageW(g_mainWindow, WM_RUN_HOTKEY, id,
                reinterpret_cast<LPARAM>(GetForegroundWindow())))
            {
                return 1;
            }
            g_suppressedKeys[vk] = false;
            --g_suppressedKeyCount;
            g_commandBusy = false;
        }
    }
    return CallNextHookEx(nullptr, code, message, param);
}

DWORD WINAPI KeyboardHookThread(void*)
{
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); // Create the thread queue.
    for (UINT vk = 0; vk < g_keysDown.size(); ++vk)
        g_keysDown[vk] = IsKeyDown(vk);

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, hInst, 0);
    g_hookError = hook ? ERROR_SUCCESS : GetLastError();
    SetEvent(g_hookReady);
    if (!hook)
        return 1;

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnhookWindowsHookEx(hook);
    return 0;
}

bool StartKeyboardHook()
{
    g_hookReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hookReady)
        return false;
    g_hookThread = CreateThread(nullptr, 0, KeyboardHookThread, nullptr, 0,
        &g_hookThreadId);
    if (!g_hookThread)
    {
        CloseHandle(g_hookReady);
        g_hookReady = nullptr;
        return false;
    }
    WaitForSingleObject(g_hookReady, INFINITE);
    CloseHandle(g_hookReady);
    g_hookReady = nullptr;
    if (g_hookError != ERROR_SUCCESS)
    {
        WaitForSingleObject(g_hookThread, INFINITE);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
        return false;
    }
    g_hotkeysEnabled = true;
    return true;
}

void StopKeyboardHook()
{
    g_hotkeysEnabled = false;
    if (g_hookThread)
    {
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_hookThread, INFINITE);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
    }
}

// ------------------------------------------------------------
// Tray icon
// ------------------------------------------------------------

bool AddTrayIcon(HWND hwnd)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);

    nid.hWnd = hwnd;
    nid.uID = TRAY_ICON_ID;

    nid.uFlags =
        NIF_MESSAGE |
        NIF_ICON |
        NIF_TIP;

    nid.uCallbackMessage =
        WM_TRAYICON;

    nid.hIcon =
        LoadIconW(
            hInst,
            MAKEINTRESOURCE(IDI_BOLDGEN));

    if (!nid.hIcon)
        nid.hIcon =
        LoadIconW(nullptr, IDI_APPLICATION);

    wcscpy_s(
        nid.szTip,
        L"BoldGen");

    return
        Shell_NotifyIconW(
            NIM_ADD,
            &nid) != FALSE;
}

void RemoveTrayIcon(HWND hwnd)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ICON_ID;

    Shell_NotifyIconW(
        NIM_DELETE,
        &nid);
}

void ShowTrayMenu(HWND hwnd)
{
    HMENU menu =
        CreatePopupMenu();

    if (!menu)
        return;

    AppendMenuW(
        menu,
        MF_STRING,
        ID_TRAY_SETTINGS,
        L"Settings");

    AppendMenuW(
        menu,
        MF_STRING,
        ID_TRAY_EXIT,
        L"Exit");

    POINT pt{};
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);

    UINT command =
        TrackPopupMenu(
            menu,
            TPM_RETURNCMD |
            TPM_RIGHTBUTTON,
            pt.x,
            pt.y,
            0,
            hwnd,
            nullptr);

    DestroyMenu(menu);

    if (command != 0)
    {
        SendMessageW(
            hwnd,
            WM_COMMAND,
            command,
            0);
    }

    PostMessageW(
        hwnd,
        WM_NULL,
        0,
        0);
}

// ------------------------------------------------------------
// Settings dialog helpers
// ------------------------------------------------------------

BYTE ModifiersToHotkeyFlags(UINT modifiers)
{
    BYTE flags = 0;

    if (modifiers & MOD_CONTROL)
        flags |= HOTKEYF_CONTROL;

    if (modifiers & MOD_ALT)
        flags |= HOTKEYF_ALT;

    if (modifiers & MOD_SHIFT)
        flags |= HOTKEYF_SHIFT;

    return flags;
}

UINT HotkeyFlagsToModifiers(BYTE flags)
{
    UINT modifiers = 0;

    if (flags & HOTKEYF_CONTROL)
        modifiers |= MOD_CONTROL;

    if (flags & HOTKEYF_ALT)
        modifiers |= MOD_ALT;

    if (flags & HOTKEYF_SHIFT)
        modifiers |= MOD_SHIFT;

    return modifiers;
}

bool IsReservedInternalHotkey(
    const Hotkey& hk)
{
    UINT modifiers =
        hk.modifiers &
        (MOD_CONTROL |
            MOD_ALT |
            MOD_SHIFT);

    if (modifiers != MOD_CONTROL)
        return false;

    return
        hk.vk == 'C' ||
        hk.vk == 'V';
}

void ShowSettings(HWND owner)
{
    g_pendingHotkeys = g_hotkeys;

    // Keep the hook alive to track releases, but let shortcuts reach the controls.
    g_hotkeysEnabled = false;
    FinishCommand();

    INT_PTR result =
        DialogBoxW(
            hInst,
            MAKEINTRESOURCE(IDD_SETTINGS),
            owner,
            SettingsProc);

    if (result == IDOK)
    {
        AcquireSRWLockExclusive(&g_hotkeyLock);
        g_hotkeys = g_pendingHotkeys;
        ReleaseSRWLockExclusive(&g_hotkeyLock);
        SaveHotkeys();
    }
    g_hotkeysEnabled = true;
}

INT_PTR CALLBACK SettingsProc(
    HWND hDlg,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        // Show the caption icon and give this owned dialog its own taskbar button.
        SetWindowLongPtrW(hDlg, GWL_EXSTYLE,
            (GetWindowLongPtrW(hDlg, GWL_EXSTYLE) &
                ~(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW)) | WS_EX_APPWINDOW);

        const HICON smallIcon = static_cast<HICON>(LoadImageW(
            hInst, MAKEINTRESOURCEW(IDI_BOLDGEN), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        const HICON largeIcon = static_cast<HICON>(LoadImageW(
            hInst, MAKEINTRESOURCEW(IDI_BOLDGEN), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        SetWindowPos(hDlg, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        constexpr const wchar_t* styleNames[HOTKEY_COUNT] =
        {
            L"Bold Serif", L"Bold Sans", L"Bold Italic Serif",
            L"Bold Italic Sans", L"Double Struck", L"Code", L"Reset"
        };

        for (int i = 0;
            i < HOTKEY_COUNT;
            ++i)
        {
            // Each static label immediately precedes its hotkey control in the
            // dialog template. Preview the same Unicode style the action applies.
            const HWND label = GetWindow(GetDlgItem(hDlg, g_hotkeyControlIds[i]),
                GW_HWNDPREV);
            const std::wstring caption = TransformText(styleNames[i],
                static_cast<TextStyle>(i));
            SetWindowTextW(label, caption.c_str());

            BYTE flags =
                ModifiersToHotkeyFlags(
                    g_pendingHotkeys[i].modifiers);

            WORD value =
                MAKEWORD(
                    static_cast<BYTE>(
                        g_pendingHotkeys[i].vk),
                    flags);

            SendDlgItemMessageW(
                hDlg,
                g_hotkeyControlIds[i],
                HKM_SETHOTKEY,
                value,
                0);
        }

        return TRUE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            std::array<Hotkey, HOTKEY_COUNT> temp{};

            for (int i = 0;
                i < HOTKEY_COUNT;
                ++i)
            {
                WORD value =
                    static_cast<WORD>(
                        SendDlgItemMessageW(
                            hDlg,
                            g_hotkeyControlIds[i],
                            HKM_GETHOTKEY,
                            0,
                            0));

                BYTE vk =
                    LOBYTE(value);

                BYTE flags =
                    HIBYTE(value);

                if (vk == 0 || IsModifierKey(vk))
                {
                    MessageBoxW(
                        hDlg,
                        L"Please assign all hotkeys.",
                        L"BoldGen",
                        MB_OK | MB_ICONWARNING);

                    return TRUE;
                }

                temp[i].vk = vk;
                temp[i].modifiers =
                    HotkeyFlagsToModifiers(flags);

                if (IsReservedInternalHotkey(
                    temp[i]))
                {
                    MessageBoxW(
                        hDlg,
                        L"Ctrl+C and Ctrl+V cannot be used "
                        L"as BoldGen hotkeys because BoldGen "
                        L"uses them internally.",
                        L"BoldGen",
                        MB_OK | MB_ICONWARNING);

                    return TRUE;
                }
            }

            // Check duplicate shortcuts.
            for (int i = 0;
                i < HOTKEY_COUNT;
                ++i)
            {
                for (int j = i + 1;
                    j < HOTKEY_COUNT;
                    ++j)
                {
                    if (temp[i].vk ==
                        temp[j].vk &&
                        temp[i].modifiers ==
                        temp[j].modifiers)
                    {
                        MessageBoxW(
                            hDlg,
                            L"Two actions have the same hotkey.",
                            L"BoldGen",
                            MB_OK | MB_ICONWARNING);

                        return TRUE;
                    }
                }
            }

            g_pendingHotkeys = temp;

            EndDialog(
                hDlg,
                IDOK);

            return TRUE;
        }

        case IDCANCEL:
            EndDialog(
                hDlg,
                IDCANCEL);

            return TRUE;
        }

        break;
    }
    }

    return FALSE;
}

// ------------------------------------------------------------
// WinMain
// ------------------------------------------------------------

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;

    InitCommonControlsEx(&icc);

    LoadStringW(
        hInstance,
        IDS_APP_TITLE,
        szTitle,
        MAX_LOADSTRING);

    LoadStringW(
        hInstance,
        IDC_BOLDGEN,
        szWindowClass,
        MAX_LOADSTRING);

    MyRegisterClass(hInstance);

    g_taskbarCreated =
        RegisterWindowMessageW(
            L"TaskbarCreated");

    LoadHotkeys();

    if (!InitInstance(
        hInstance,
        SW_HIDE))
    {
        return FALSE;
    }

    MSG msg{};

    while (GetMessageW(
        &msg,
        nullptr,
        0,
        0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(
        msg.wParam);
}

// ------------------------------------------------------------
// Window class
// ------------------------------------------------------------

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};

    wcex.cbSize =
        sizeof(WNDCLASSEXW);

    wcex.lpfnWndProc =
        WndProc;

    wcex.hInstance =
        hInstance;

    wcex.hIcon =
        LoadIconW(
            hInstance,
            MAKEINTRESOURCE(IDI_BOLDGEN));

    wcex.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);

    wcex.lpszClassName =
        szWindowClass;

    wcex.hIconSm =
        LoadIconW(
            hInstance,
            MAKEINTRESOURCE(IDI_SMALL));

    // Important:
    // no menu and no visible-window-specific styles.
    wcex.lpszMenuName = nullptr;

    return RegisterClassExW(
        &wcex);
}

// ------------------------------------------------------------
// Hidden window
// ------------------------------------------------------------

BOOL InitInstance(
    HINSTANCE hInstance,
    int)
{
    hInst = hInstance;

    // Hidden top-level window.
    //
    // Do NOT call ShowWindow().
    g_mainWindow =
        CreateWindowExW(
            0,
            szWindowClass,
            szTitle,
            0,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            hInstance,
            nullptr);

    if (!g_mainWindow)
        return FALSE;

    if (!AddTrayIcon(
        g_mainWindow))
    {
        DestroyWindow(
            g_mainWindow);

        return FALSE;
    }

    if (!StartKeyboardHook())
    {
        MessageBoxW(
            nullptr,
            L"BoldGen could not install its keyboard hook. Please restart BoldGen.",
            L"BoldGen",
            MB_OK | MB_ICONERROR);
        DestroyWindow(g_mainWindow);
        return FALSE;
    }

    return TRUE;
}

// ------------------------------------------------------------
// Main hidden-window message procedure
// ------------------------------------------------------------

LRESULT CALLBACK WndProc(
    HWND hWnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (g_taskbarCreated != 0 && message == g_taskbarCreated)
    {
        // Explorer.exe restarted.
        // Re-create the tray icon.
        AddTrayIcon(hWnd);
        return 0;
    }

    switch (message)
    {
    case WM_TRAYICON:
    {
        if (wParam != TRAY_ICON_ID)
            break;

        switch (lParam)
        {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu(hWnd);
            break;

        case WM_LBUTTONDBLCLK:
            ShowSettings(hWnd);
            break;
        }

        return 0;
    }

    case WM_RUN_HOTKEY:
    {
        HandleHotkey(
            static_cast<int>(wParam), reinterpret_cast<HWND>(lParam));

        return 0;
    }

    case WM_TIMER:
        if (wParam == COMMAND_TIMER)
        {
            AdvanceCommand();
            return 0;
        }
        break;

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_TRAY_SETTINGS:
            ShowSettings(hWnd);
            return 0;

        case ID_TRAY_EXIT:
            RequestExit();
            return 0;
        }

        break;
    }

    case WM_CLOSE:
        RequestExit();
        return 0;

    case WM_DESTROY:
        StopKeyboardHook();
        ClearCommand();
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hWnd,
        message,
        wParam,
        lParam);
}
