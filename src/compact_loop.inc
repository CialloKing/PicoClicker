// Both editions run the same wait, timer and input paths. The dialog resource
// determines whether the callback needs a buffer of seven bytes or seven UTF-16 units.
#include "compact_cookie.h"
#ifdef PICOCLICKER_WIDE_DIALOG
#define EDIT_UNIT word
#define NEXT_DIGIT(reg) add reg, 2
.set .Ledit_space, 16
#else
#define EDIT_UNIT byte
#define NEXT_DIGIT(reg) inc reg
.set .Ledit_space, 8
#endif
// Win64 reserves 48 bytes for outgoing arguments, followed by the edit and cookie.
.set .Ldialog_stack, (48 + .Ledit_space + 8 + 15) & -16

// The loader's cookie immediately precedes the first IAT slot in both images.
.section .idata$4,"dr"
#ifdef __i386__
.set .Liat_base_offset, 0
.p2align 2
.globl ___security_cookie
___security_cookie:
    .long INITIAL_COOKIE
#else
.set .Liat_base_offset, .Liat_GetAncestor
.p2align 3
.globl __security_cookie
__security_cookie:
    .quad INITIAL_COOKIE
#endif
.set .Lcookie_disp, -__SIZEOF_POINTER__ - .Liat_base_offset
.if .Lcookie_disp < -128
    .error "The stack cookie must be within 128 bytes before the IAT base"
.endif

#ifdef __x86_64__
.macro access_iat_cookie opcode, modrm
    // The x64 IAT base comes from a label difference, so force the checked disp8.
    .byte 0x48, \opcode, \modrm, .Lcookie_disp
.endm
#endif

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

#ifdef __i386__
#include "compact_run_x86.inc"
#else
#include "compact_run_x64.inc"
#endif

#ifndef PICOCLICKER_WIDE_DIALOG
// The entry ends at ExitProcess. Use the padding before the CFG-aligned callback.
.Lstop_label:  .asciz "Stop"
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
