// Keep the normal Unicode resource dialog and C implementation. Resolve the
// unbound imports through FirstThunk so the loader needs only one thunk table.
#ifdef __i386__
.set @feat.00, 0x801               // This object has no SEH handlers.
#else
.set @feat.00, 0x800
#endif

#include "compact_iat.inc"

.Lkernel_iat:
    import CancelWaitableTimer, 4
    import CreateWaitableTimerExW, 16
    import ExitProcess, 4
    import SetWaitableTimer, 24
    end_iat

.Luser_iat:
    import CreateDialogParamW, 20
    import DispatchMessageW, 4
    import EnableWindow, 8
    import GetAncestor, 8
    import GetCursorPos, 4
    import GetDlgItem, 8
    import IsDialogMessageW, 8
    import MessageBoxW, 16
    import MsgWaitForMultipleObjectsEx, 20
    import PeekMessageW, 20
    import RegisterHotKey, 16
    import SendDlgItemMessageW, 20
    import SendInput, 12
    import SetProcessDpiAwarenessContext, 4
    import TranslateMessage, 4
    import WindowFromPoint, 8
    end_iat

.section .idata$7,"dr"
.Lkernel_name:
    .asciz "KERNEL32.dll"
.Luser_name:
    .asciz "USER32.dll"

.intel_syntax noprefix
.text
.Lguard_check_fallback:
    ret

#include "compact_guard_slots.inc"
