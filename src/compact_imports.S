#include "resource.h"

.if IDC_MIDDLE != IDC_LEFT + 1 || IDC_RIGHT != IDC_LEFT + 2
    .error "Mouse button flags must match the radio control IDs"
.endif
.if IDC_TOGGLE != IDC_RIGHT + 1
    .error "The toggle ID must follow the mouse button IDs"
.endif

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

#define CREATE_DIALOG_API CreateDialogIndirectParamA
#define DIALOG_TITLE .Ltitle
#define UI_API(x) x##A

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
.p2align 2
#else
.p2align 3
#endif
.Liat_text_begin:
.Lui_error:    .asciz "UI Err"
.Lclick_error: .asciz "Click Err"
.Linterval:    .asciz "1-60000ms"
.Lbusy:        .asciz "F1 Busy"
.Lf1:          .asciz "F1"
.Lon:          .asciz "On"
.Lwait_error:
.Ltimer_error: .asciz "Timer Err"

// Both callbacks index these flags from their IAT base; the cookie stays aligned.
.Lbutton_flags:
    .byte 2, 32, 8                 // MOUSEEVENTF_*DOWN in control ID order.
.set .Liat_text_size, (. - .Liat_text_begin + __SIZEOF_POINTER__ - 1) & -__SIZEOF_POINTER__
.macro load_button_flags
    .set .Lbutton_disp, .Lbutton_flags - .Liat_text_begin - .Liat_text_size + .Lcookie_disp
    .if .Lbutton_disp < -128
        .error "Mouse button flags must be within 128 bytes before the IAT"
    .endif
    // MOVZX EAX,[(E/R)BX+(E/R)AX+disp8]. LLVM reserves disp32 for the label difference.
    .byte 0x0f, 0xb6, 0x44, 0x03, .Lbutton_disp
.endm

.macro load_status_text name
    .set .Ltext_disp\@, .L\name - .Liat_text_begin - .Liat_text_size + .Lcookie_disp
    .if .Ltext_disp\@ < -128 || .Ltext_disp\@ >= 0
        .error "Read-only text must be within 128 bytes before the IAT base"
    .endif
    // LLVM reserves disp32 for label differences. Emit LEA with disp8.
#ifdef __x86_64__
    .byte 0x48
#endif
    .byte 0x8d, 0x53, .Ltext_disp\@
.endm

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

#include "compact_loop.inc"
