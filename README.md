# BoldGen

A small Windows tray utility that converts selected text into styled Unicode
characters using customizable keyboard shortcuts. Written in C++ with the Win32
API, BoldGen runs in the notification area and opens a window only for Settings.

This repository contains source code, not prebuilt binaries. Build the project
locally to use it.

## Build from source

### Requirements

- Windows
- Visual Studio 2026 with the **Desktop development with C++** workload
- MSVC v145 toolset and a Windows SDK with version `10.0.*`

The project uses C++20 and has no third-party library dependencies.

### Visual Studio

1. Clone or download this repository.
2. Open `BoldGen.slnx` in Visual Studio.
3. Select **Release** and **x64**. Debug and x86 configurations are also available.
4. Choose **Build > Build Solution**.
5. Choose **Debug > Start Without Debugging** (`Ctrl+F5`) to launch the built app.

For a command-line build, open a Developer PowerShell for Visual Studio in the
repository root:

```powershell
msbuild .\BoldGen.slnx /p:Configuration=Release /p:Platform=x64
```

## Usage

After launching the app, look for the **B** icon in the notification area. It may
be inside the tray's hidden-icons menu.

1. Right-click the tray icon and choose **Settings** to configure shortcuts.
2. Select text in an application that supports `Ctrl+C` and `Ctrl+V`.
3. Press the desired shortcut, then release all of its keys.

BoldGen backs up the clipboard, copies the selection, applies the chosen style,
pastes it back, and restores the previous clipboard contents.
Choose **Reset** to restore text decorated with one of the supported styles.
You can also switch directly from one style to another without resetting first.

### Styles and default shortcuts

| Style | Default shortcut |
| --- | --- |
| 𝐁𝐨𝐥𝐝 𝐒𝐞𝐫𝐢𝐟 | `Ctrl+Alt+1` |
| 𝗕𝗼𝗹𝗱 𝗦𝗮𝗻𝘀 | `Ctrl+Alt+2` |
| 𝑩𝒐𝒍𝒅 𝑰𝒕𝒂𝒍𝒊𝒄 𝑺𝒆𝒓𝒊𝒇 | `Ctrl+Alt+3` |
| 𝘽𝙤𝙡𝙙 𝙄𝙩𝙖𝙡𝙞𝙘 𝙎𝙖𝙣𝙨 | `Ctrl+Alt+4` |
| 𝔻𝕠𝕦𝕓𝕝𝕖 𝕊𝕥𝕣𝕦𝕔𝕜 | `Ctrl+Alt+5` |
| 𝙲𝚘𝚍𝚎 | `Ctrl+Alt+6` |
| Reset | `Ctrl+Alt+7` |

Click **OK** to save shortcut changes or **Cancel** to discard them. Preferences
are saved for the current Windows user. Choose **Exit** from the tray menu to
close BoldGen.

### Settings options

- **Run at startup** starts BoldGen in the tray when the current Windows user signs
  in. Check or uncheck it and click **OK** to add or remove BoldGen's entry in the
  user's `Software\Microsoft\Windows\CurrentVersion\Run` registry key. The entry
  uses the current executable's path; keep that build in a stable location.
- **?** opens the About dialog.
- **Eye / crossed eye** toggles whether Settings and About appear in screen
  captures. The eye means visible; the crossed eye means excluded. The change is
  previewed immediately; **OK** saves it for future launches and **Cancel** discards
  it. The compact **?** and eye buttons are in the top-right corner; the startup
  checkbox shares the bottom row with **OK** and **Cancel**.

Capture exclusion uses [`WDA_EXCLUDEFROMCAPTURE`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity),
supported starting with Windows 10 version 2004. Older Windows versions may show
a blank window in captures instead of excluding it. This setting applies to
BoldGen's dialogs, not to the application whose text is being transformed.

## Behavior and limitations

- Styles replace characters with Unicode equivalents; they do not apply rich-text
  font formatting. Latin letters (`A–Z`, `a–z`) and digits are converted where the
  selected style has equivalents. The italic styles leave digits plain. Other
  characters, including punctuation and emoji, are preserved.
- Clipboard backup preserves the available formats, including text, rich text,
  images, and file lists. An originally empty clipboard is restored to empty.
  If a format cannot be backed up safely, the command is cancelled before copying.
- Restoration waits 500 ms after sending paste to give the target application time
  to read the transformed text. This is a timing allowance, not a paste-completion
  acknowledgment; unusually slow applications may need longer. Clipboard access
  failures are retried for up to two seconds during restoration and logged if
  restoration cannot complete.
- If the clipboard changes again before restoration, the newer contents are kept.
  Cancelled or failed operations also restore the backup when their copy changed
  the clipboard. Opening Settings or choosing Exit allows pending restoration
  to finish.
- A command is cancelled if the foreground window or focused control changes.
  If copying does not produce fresh text, nothing is pasted.
- Shortcuts are paused while Settings is open. Repeated key presses and
  overlapping commands do not start additional conversions.
- Keyboard hooks avoid conflicts with `RegisterHotKey` registrations, but another
  hook can still consume a shortcut before BoldGen receives it. Windows also
  restricts sending input to applications running at a higher integrity level.

## Development

### Regression checks

From the repository root, run:

```powershell
.\tests\run-tests.cmd
```

The script runs simulated regression checks and a native integration test using
the real Windows clipboard and an EDIT control in an isolated window station.
Keyboard delivery is simulated; the tests do not install a global hook or change
your desktop clipboard. Coverage includes modifier matching,
key repeats, injected input, delayed copy, clipboard contention, focus changes,
input failures, Unicode conversions, and clipboard backup/restoration (including
multiple formats, images, cancellation, and preservation of newer clipboard data).
The native test checks all seven styles and Windows-synthesized clipboard formats.
It also exercises the Settings controls, About dialog, OK/Cancel behavior, and
startup preferences using a temporary, redirected registry location. Capture API
calls are simulated in that isolated desktop test; it verifies the requested
flags and UI state, not the resulting output of screen-recording software.

For a live check, launch your build, select text in Notepad, and try each style
and Reset. Test any conflicting shortcut with the other application running;
these desktop interactions are not covered by the simulated tests. Exit older
BoldGen instances before testing a new build.

### Implementation notes

A dedicated thread runs the `WH_KEYBOARD_LL` hook. The main window uses a timer to
wait for key release (up to five seconds), fresh clipboard text (up to two
seconds), paste readiness, and clipboard restoration. Failures are sent to the
debugger's Output window with a `BoldGen:` prefix. Backup, input-injection, and
copy-timeout failures also display a tray notification instead of failing silently.

The icon resources can be regenerated with `tools\Generate-Icons.ps1`.

Win32 references: [keyboard hooks](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
and [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).
