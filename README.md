# BoldGen

Run `x64\Release\BoldGen.exe`. BoldGen starts in the notification area; right-click
its icon for Settings or Exit. Settings retains the existing saved shortcuts.

Select text in an editor, press a configured shortcut, then release its keys.
BoldGen copies the selection, converts it, and pastes the result. The transformed
text stays on the clipboard. Reset converts BoldGen's supported styles back to
plain letters and digits; other characters are preserved.

## Hotkey handling

Shortcuts use a `WH_KEYBOARD_LL` hook instead of `RegisterHotKey`, so another
application registering the same combination does not prevent BoldGen from
installing its shortcuts. A matched shortcut consumes the action key's down,
repeat, and up events. Injected keyboard events pass through. Shortcut handling
is paused while Settings is open, and overlapping commands are ignored.

The hook runs on a dedicated thread. The main window uses a timer to wait for
key release (up to five seconds), fresh clipboard text (up to two seconds), and
paste readiness. It cancels if the foreground window or focused control changes.
Failures are reported to the debugger's Output window with a `BoldGen:` prefix.

A hook cannot guarantee priority over another hook that consumes input before
BoldGen sees it. It also does not bypass Windows' restrictions on sending input
to applications running at a higher integrity level.

References: [keyboard hook behavior](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
and [SendInput keyboard state and integrity restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

## Build and verification

Open `BoldGen.slnx` in Visual Studio 2026 with the C++ desktop tools installed.
Build Debug or Release for x64 or x86.

Run `tests\run-tests.cmd` for regression checks. These compile the production
callback and command pipeline with simulated keyboard/clipboard I/O; they do not
install a hook or alter the desktop clipboard. They cover repeat suppression,
modifier matching, injected input, settings suspension, delayed copy, clipboard
contention, focus changes, failed input, and all supported Unicode conversions.

For a live check, exit any older BoldGen instance, run the rebuilt executable,
select text in Notepad, and press/release each configured shortcut. Check Reset
on already decorated text. To verify a particular conflict, leave the other
application running with its shortcut registered and repeat using that shortcut
in BoldGen. This desktop interaction is not covered by the simulated tests.
