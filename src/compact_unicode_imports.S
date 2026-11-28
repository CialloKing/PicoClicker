// Both Unicode editions resolve their unbound imports through FirstThunk.
#ifdef __i386__
.set @feat.00, 0x801               // This object has no SEH handlers.
#else
.set @feat.00, 0x800
#endif

#if defined(PICOCLICKER_LITE) && defined(__x86_64__)
// The read-only names fit after the Lite code without growing its file block.
#define HINT_SECTION .section .text$names,"xr"
#endif
#include "compact_iat.inc"

// Hotkey capture and its settings file are only present in the full dialog.
.macro full_import name, stdcall_bytes
#ifndef PICOCLICKER_LITE
    import \name, \stdcall_bytes
#endif
.endm

.Lkernel_iat:
    import CancelWaitableTimer, 4
    full_import CloseHandle, 4
    full_import CreateFileW, 28
    import CreateWaitableTimerExW, 16
    full_import DeleteFileW, 4
    import ExitProcess, 4
    full_import ExpandEnvironmentStringsW, 12
    full_import GetCurrentProcessId, 0
    full_import MoveFileExW, 12
    full_import QueryPerformanceCounter, 4
    full_import ReadFile, 20
    import SetWaitableTimer, 24
    full_import WriteFile, 20
    end_iat

.Luser_iat:
    import CreateDialogParamW, 20
    full_import DestroyWindow, 4
    import DispatchMessageW, 4
    import EnableWindow, 8
    import GetAncestor, 8
    full_import GetAsyncKeyState, 4
    import GetCursorPos, 4
    import GetDlgItem, 8
    full_import GetKeyNameTextW, 12
    full_import GetKeyState, 4
    import IsDialogMessageW, 8
    full_import KillTimer, 8
    full_import MapVirtualKeyW, 8
    import MessageBoxW, 16
    import MsgWaitForMultipleObjectsEx, 20
    import PeekMessageW, 20
    full_import PostQuitMessage, 4
    import RegisterHotKey, 16
    import SendDlgItemMessageW, 20
    import SendInput, 12
    import SetProcessDpiAwarenessContext, 4
    full_import SetTimer, 16
    import TranslateMessage, 4
    full_import UnregisterHotKey, 8
    import WindowFromPoint, 8
    end_iat

.section .idata$7,"dr"
.Lkernel_name:
    .asciz "KERNEL32.dll"
.Luser_name:
    .asciz "USER32.dll"

#ifdef PICOCLICKER_LITE
#define PICOCLICKER_WIDE_DIALOG
#define UI_API(x) x##W
#define CREATE_DIALOG_API CreateDialogParamW
#ifdef __i386__
#define DIALOG_TITLE _PicoText_title
#else
#define DIALOG_TITLE PicoText_title
#endif

.macro load_status_text name
#ifdef __i386__
    mov edx, offset _PicoText_\name
#else
    lea rdx, [rip + PicoText_\name]
#endif
.endm

#include "compact_loop.inc"
#else
.intel_syntax noprefix
.text
.Lguard_check_fallback:
    ret

#include "compact_guard_slots.inc"
#endif
