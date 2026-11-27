// The experimental image resolves its unbound imports through FirstThunk.
// Use the explicit .gfids list for CFG. The other calls are direct or use fixed IAT slots.
// Make the omitted guard counts visible to pack.ps1 without allocating bytes for them.
#ifdef __i386__
// The x86 assembly installs no SEH handlers.
.set @feat.00, 0x801
.globl ___guard_iat_count, ___guard_longjmp_count
#else
.set @feat.00, 0x800
.globl __guard_iat_count, __guard_longjmp_count
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
    .set .Liat_\name, . - .Lkernel_iat
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
    import CreateWaitableTimerExA, 16
    import ExitProcess, 4
    import SetWaitableTimer, 24
    end_iat

.Luser_iat:
    import DestroyWindow, 4
    import DispatchMessageA, 4
    import EnableWindow, 8
    import GetAncestor, 8
    import GetCursorPos, 4
    import GetDlgItem, 8
    import IsDialogMessageA, 8
    import MessageBoxA, 16
    import MsgWaitForMultipleObjectsEx, 20
    import PeekMessageA, 20
    import PostQuitMessage, 4
    import RegisterHotKey, 16
    import SendDlgItemMessageA, 20
    import SendInput, 12
    import SetProcessDpiAwarenessContext, 4
    import TranslateMessage, 4
    // Together with the DLL names, these trailing names fill the PE header space.
    import CreateDialogIndirectParamA, 20
    import WindowFromPoint, 8
    end_iat

.section .idata$7,"dr"
.Lkernel_name:
    .asciz "KERNEL32.dll"
.Luser_name:
    .asciz "USER32.dll"

// LLVM otherwise reserves a four-byte displacement for these section-relative constants.
// The same FF /2 encoding calls through EBX in PE32 and RBX in PE32+.
.macro call_import name, modrm=0x53
#ifdef __i386__
    .set .Ldisp\@, .Liat_\name
#else
    .set .Ldisp\@, .Liat_\name - .Liat_GetAncestor
#endif
    .if .Ldisp\@ < -128 || .Ldisp\@ > 127
        .error "Compact IAT displacement does not fit in one byte"
    .endif
    .byte 0xff, \modrm, .Ldisp\@
.endm

.macro tail_import name
    call_import \name, 0x63
.endm

.section .rdata,"dr"
.Ldialog_encoding:
    .include PICO_DIALOG_TEMPLATE

.Lui_error:    .asciz "UI Err"
.Lclick_error: .asciz "Click Err"
.Ltimer_error: .asciz "Timer Err"
.Linterval:    .asciz "1-60000ms"
.Lbusy:        .asciz "F1 Busy"
.Lf1:          .asciz "F1"
.Lon:          .asciz "On"
.Lstop_label:  .asciz "Stop"

.bss
.p2align 2
.Ldialog_template:
    .space 2 * .Ldialog_words

// The encoder sizes the DWORD-aligned buffer and stream from the same RC template.
.macro decode_dialog
    mov ecx, .Ldialog_words
    cld
.Ldecode_dialog\@:
    xor eax, eax
    lodsb
    cmp al, 254
    jb .Lstore_word\@
    cbw                            // 255 encodes the template word 0xffff.
    jne .Lstore_word\@
    lodsw
.Lstore_word\@:
    stosw
    loop .Ldecode_dialog\@
.endm

#ifdef __i386__
#include "compact_run_x86.inc"
#include "compact_dialog_x86.inc"
#else
#include "compact_run_x64.inc"
#include "compact_dialog_x64.inc"
#endif
