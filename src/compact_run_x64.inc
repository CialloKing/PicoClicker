#include "resource.h"

.intel_syntax noprefix

.set .Lgeneration, 0
.set .Ltimer, 8
.set .Lidle_status, 16
.set .Lclick_pair, 24
.set .Lmouse_flags, 20
.set .Linput_size, 40

// The dialog manager invokes this callback indirectly. Its address is taken here, outside C.
.section .gfids$y,"dr"
.symidx DialogProc

.text
.globl Run
.def Run
    .scl 2
    .type 32
.endef
.seh_proc Run
Run:
    push rbx
    .seh_pushreg rbx
    push rsi
    .seh_pushreg rsi
    push rdi
    .seh_pushreg rdi
    push rbp
    .seh_pushreg rbp
    push r12
    .seh_pushreg r12
    sub rsp, 112
    .seh_stackalloc 112
    .seh_endprologue

    // After the shadow space: fifth argument, wait result, MSG, then the /GS cookie.
    mov rax, qword ptr [rip + __security_cookie]
    xor rax, rsp
    mov qword ptr [rsp + 96], rax
    // Center the IAT base so every call can use a signed-byte displacement.
    lea rbx, [rip + __imp_GetAncestor]
    lea rdi, [rip + picoRunData]
    lea rbp, [rsp + 48]
    lea rdx, [rip + .Ldialog_template]
    xor r8d, r8d
    lea r9, [rip + DialogProc]
    mov qword ptr [rsp + 32], r8
    call_import CreateDialogIndirectParamA
    test rax, rax
    jz .Lui_failed
    mov rsi, rax

.Lwait:
    mov r12d, dword ptr [rdi + .Lgeneration]
    mov ecx, r12d
    and ecx, 1
    lea rdx, [rdi + .Ltimer]
    or r8d, -1                     // INFINITE
    mov r9d, 0x1cff                // QS_ALLINPUT
    mov dword ptr [rsp + 32], 4    // MWMO_INPUTAVAILABLE
    call_import MsgWaitForMultipleObjectsEx
    inc eax
    jz .Lwait_failed               // WAIT_FAILED was -1
    mov dword ptr [rsp + 40], eax
    mov byte ptr [rsp + 44], 32
.Lpeek:
    mov rcx, rbp
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    mov dword ptr [rsp + 32], 1    // PM_REMOVE
    call_import PeekMessageA
    test eax, eax
    jz .Lcheck_tick
    cmp dword ptr [rbp + 8], 0x12  // WM_QUIT
    je .Lquit
    mov rcx, rsi
    mov rdx, rbp
    call_import IsDialogMessageA
    test eax, eax
    jnz .Ldrain
    mov rcx, rbp
    call_import TranslateMessage
    mov rcx, rbp
    call_import DispatchMessageA
.Ldrain:
    dec byte ptr [rsp + 44]
    jnz .Lpeek

.Lcheck_tick:
    // Queued stops and restarts invalidate the signal from the earlier timer generation.
    cmp dword ptr [rsp + 40], 1    // WAIT_OBJECT_0 before the increment
    jne .Lagain
    test r12b, 1
    jz .Lagain
    cmp r12d, dword ptr [rdi + .Lgeneration]
    je .Lclick
.Lagain:
    jmp .Lwait

.Lclick:
    // Win64 passes an eight-byte POINT by value. Reuse the now-idle MSG buffer.
    mov rcx, rbp
    call_import GetCursorPos
    test eax, eax
    jz .Lagain
    mov rcx, qword ptr [rbp]
    call_import WindowFromPoint
    mov rcx, rax
    mov edx, 2                     // GA_ROOT
    call_import GetAncestor
    cmp rax, rsi
    je .Lagain
    mov ecx, 2
    lea rdx, [rdi + .Lclick_pair]
    mov r8d, .Linput_size
    call_import SendInput
    cmp eax, 2
    je .Lagain
    dec eax
    jnz .Lclick_failed
    // Release the button if SendInput accepted only the press.
    mov ecx, 1
    lea rdx, [rdi + .Lclick_pair + .Linput_size]
    mov r8d, .Linput_size
    call_import SendInput
.Lclick_failed:
    lea rdx, [rip + .Lclick_error]
    mov al, 1
    call .Lupdate_state
    jmp .Lwait

.Lwait_failed:
    xor edx, edx
    mov al, 1
    call .Lupdate_state
    mov rcx, rsi
    lea rdx, [rip + .Ltimer_error]
    call .Lrun_error
    mov rcx, rsi
    call_import DestroyWindow
    jmp .Lfailed
.Lui_failed:
    xor ecx, ecx
    lea rdx, [rip + .Lui_error]
    call .Lrun_error
.Lfailed:
    mov esi, 1
    jmp .Lreturn
.Lquit:
    mov esi, dword ptr [rbp + 16]
.Lreturn:
    mov rcx, qword ptr [rsp + 96]
    xor rcx, rsp
    call __security_check_cookie
    mov eax, esi
    add rsp, 112
    pop r12
    pop rbp
    pop rdi
    pop rsi
    pop rbx
    ret
.seh_endproc

.att_syntax prefix
