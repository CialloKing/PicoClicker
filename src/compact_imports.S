// The experimental image resolves its unbound imports through FirstThunk.
#ifdef __i386__
// The data-only object is compatible with /SAFESEH.
.set @feat.00, 1
#endif

.macro descriptor dll
    .long 0, 0, 0
    .rva .L\dll\()_name
    .rva .L\dll\()_iat
.endm

.section .idata$2,"dr"
.globl PicoImportDescriptors
PicoImportDescriptors:
    descriptor kernel
    descriptor user
    // LLD appends the null import descriptor.

.macro import name, stdcall_bytes
#ifdef __i386__
    .globl "__imp__\name@\stdcall_bytes"
"__imp__\name@\stdcall_bytes":
#else
    .globl __imp_\name
__imp_\name:
#endif
    .rva .Lhint_\name
#ifdef __x86_64__
    .long 0
#endif
    // Reuse the preceding string's terminator while keeping each hint word aligned.
    // A hint may also use the preceding character. Windows checks the full name.
    .section .idata$6,"dr"
    .if (. - .Lhint_begin) % 2
        .set .Lhint_\name, . - 1
        .byte 0
    .else
        .set .Lhint_\name, . - 2
    .endif
    .asciz "\name"
    .section .idata$5,"dr"
.endm

.macro end_iat
    .long 0
#ifdef __x86_64__
    .long 0
#endif
.endm

.section .idata$6,"dr"
.p2align 1
.Lhint_begin:
    .short 0

.section .idata$5,"dr"
#ifdef __x86_64__
.p2align 3
#else
.p2align 2
#endif
.Lkernel_iat:
    import CancelWaitableTimer, 4
    import CreateWaitableTimerExW, 16
    import ExitProcess, 4
    import SetWaitableTimer, 24
    end_iat

.Luser_iat:
    import CreateDialogParamW, 20
    import DestroyWindow, 4
    import DispatchMessageW, 4
    import EnableWindow, 8
    import GetAncestor, 8
    import GetCursorPos, 4
    import GetDlgItem, 8
    import IsDialogMessageW, 8
    import MessageBoxW, 16
    import MsgWaitForMultipleObjectsEx, 20
    import PeekMessageW, 20
    import PostQuitMessage, 4
    import RegisterHotKey, 16
    import SendDlgItemMessageW, 20
    import SendInput, 12
    import SetProcessDpiAwarenessContext, 4
    import TranslateMessage, 4
    import UnregisterHotKey, 8
    import WindowFromPoint, 8
    end_iat

.section .idata$7,"dr"
.Lkernel_name:
    .asciz "KERNEL32.dll"
.Luser_name:
    .asciz "USER32.dll"
