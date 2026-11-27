#include "compact_cookie.h"
#include "resource.h"

.if IDC_MIDDLE != IDC_LEFT + 1 || IDC_RIGHT != IDC_LEFT + 2
    .error "Mouse button flags must match the radio control IDs"
.endif
.if IDC_TOGGLE != IDC_RIGHT + 1
    .error "The toggle ID must follow the mouse button IDs"
.endif

.macro button_flags
.Lbutton_flags:
    .byte 2, 32, 8                 // MOUSEEVENTF_*DOWN in control ID order.
.endm

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

#include "compact_iat.inc"

.Lkernel_iat:
    import CancelWaitableTimer, 4
    import CreateWaitableTimerExA, 16
    import ExitProcess, 4
    import SetWaitableTimer, 24
    end_iat

.Luser_iat:
    import DispatchMessageA, 4
    import GetAncestor, 8
    import GetCursorPos, 4
    import MessageBoxA, 16
    import MsgWaitForMultipleObjectsEx, 20
    import PeekMessageA, 20
    import SendDlgItemMessageA, 20
    import SendInput, 12
    import SetProcessDpiAwarenessContext, 4
    // PE32 can spread this tail across its .text padding and PE header.
    import RegisterHotKey, 16
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
    .if .Ldisp\@ == 0
        // The slot at the base needs no displacement byte.
        .byte 0xff, \modrm - 0x40
    .else
        .byte 0xff, \modrm, .Ldisp\@
    .endif
.endm

.macro tail_import name
    call_import \name, 0x63
.endm

#ifdef __x86_64__
// Moving the template lowers the .rdata floor before the import-name tail.
.section .text$dialog,"xr"
#else
.section .rdata,"dr"
#endif
.Ldialog_encoding:
    .include PICO_DIALOG_TEMPLATE

// FirstThunk uses .idata$5. The unused lookup-table contribution can hold
// read-only data immediately before it. The packer verifies the cookie offset.
.section .idata$4,"dr"
#ifdef __i386__
.set .Lcookie_width, 4
.set .Liat_base_offset, 0
.p2align 2
#else
.set .Lcookie_width, 8
.set .Liat_base_offset, .Liat_GetAncestor
.p2align 3
#endif
.Liat_text_begin:
.Lui_error:    .asciz "UI Err"
.Lclick_error: .asciz "Click Err"
.Linterval:    .asciz "1-60000ms"
.Lbusy:        .asciz "F1 Busy"
.Lf1:          .asciz "F1"
.Lon:          .asciz "On"
.Ltimer_error: .asciz "Timer Err"

#ifdef __i386__
    button_flags
#endif
.set .Liat_text_size, (. - .Liat_text_begin + .Lcookie_width - 1) & -.Lcookie_width
#ifdef __i386__
.p2align 2
.globl ___security_cookie
___security_cookie:
    .long INITIAL_COOKIE
#else
.p2align 3
.globl __security_cookie
__security_cookie:
    .quad INITIAL_COOKIE
#endif
.set .Lcookie_disp, -.Lcookie_width - .Liat_base_offset
.if .Lcookie_disp < -128
    .error "The stack cookie must be within 128 bytes before the IAT base"
.endif
#ifdef __i386__
.set .Lbutton_disp, .Lbutton_flags - .Liat_text_begin - .Liat_text_size + .Lcookie_disp
.if .Lbutton_disp < -128
    .error "Mouse button flags must be within 128 bytes before the IAT"
.endif
#endif

.macro load_iat_text name
    .set .Ltext_disp\@, \name - .Liat_text_begin - .Liat_text_size + .Lcookie_disp
    .if .Ltext_disp\@ < -128 || .Ltext_disp\@ >= 0
        .error "Read-only text must be within 128 bytes before the IAT base"
    .endif
    // LLVM reserves disp32 for label differences. Emit LEA with disp8.
#ifdef __x86_64__
    .byte 0x48
#endif
    .byte 0x8d, 0x53, .Ltext_disp\@
.endm

#ifdef __x86_64__
.macro access_iat_cookie opcode, modrm
    // The x64 IAT base comes from a label difference, so force the checked disp8.
    .byte 0x48, \opcode, \modrm, .Lcookie_disp
.endm
#endif

.bss
.p2align 2
.Ldialog_template:
    .space 2 * .Ldialog_words

// The encoder sizes the DWORD-aligned buffer and stream from the same RC template.
.macro decode_dialog
    .if .Ldialog_words <= 255
        xor ecx, ecx
        mov cl, .Ldialog_words
    .else
        mov ecx, .Ldialog_words
    .endif
    cld
.Ldecode_dialog\@:
    xor eax, eax
    lodsb
    cmp al, 253
    jne .Ldecode_word\@
    xor eax, eax                   // Emit the first of two consecutive zero words.
    stosw
    dec ecx
.Ldecode_word\@:
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
#else
#include "compact_run_x64.inc"
#endif

// The entry ends at ExitProcess. Use the padding before the CFG-aligned callback.
.Lstop_label:  .asciz "Stop"
#ifdef __x86_64__
    button_flags
#endif

#ifdef __i386__
#include "compact_dialog_x86.inc"
#else
#include "compact_dialog_x64.inc"
#endif

.intel_syntax noprefix
.text
// Both assembly frames check their decoded stack cookie before returning or exiting.
// A mismatch still terminates through Windows FAST_FAIL_STACK_COOKIE_CHECK.
.Lcheck_cookie:
#ifdef __i386__
    cmp ecx, dword ptr [ebx + .Lcookie_disp]
#else
    access_iat_cookie 0x3b, 0x4b   // CMP RCX, [RBX+disp8]
    jne .Lcookie_failed
    shr rcx, 48                    // Return with RCX zero for a valid Win64 cookie.
#endif
    jne .Lcookie_failed
.Lguard_check_fallback:
    ret
.Lcookie_failed:
    mov ecx, 2                     // FAST_FAIL_STACK_COOKIE_CHECK
    int 0x29
    int3

#include "compact_guard_slots.inc"
