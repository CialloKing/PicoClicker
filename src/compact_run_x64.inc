#include "resource.h"

.intel_syntax noprefix

.section .rdata,"dr"
.Lui_error:
    .asciz "UI Err"
.Lclick_error:
    .asciz "Click Err"

// CreateDialogParamA invokes this callback indirectly. Its address is taken here, outside C.
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
    lea rdi, [rsp + 48]
    mov edx, IDD_MAIN
    xor r8d, r8d
    lea r9, [rip + DialogProc]
    mov qword ptr [rsp + 32], r8
    call_import CreateDialogParamA
    test rax, rax
    jz .Lui_failed
    mov rsi, rax

.Lwait:
    mov r12d, dword ptr [rip + runState]
    mov ecx, r12d
    and ecx, 1
    lea rdx, [rip + timer]
    mov r8d, -1                    // INFINITE
    mov r9d, 0x1cff                // QS_ALLINPUT
    mov dword ptr [rsp + 32], 4    // MWMO_INPUTAVAILABLE
    call_import MsgWaitForMultipleObjectsEx
    inc eax
    jz .Lwait_failed               // WAIT_FAILED was -1
    mov dword ptr [rsp + 40], eax
    mov ebp, 32
.Lpeek:
    mov rcx, rdi
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    mov dword ptr [rsp + 32], 1    // PM_REMOVE
    call_import PeekMessageA
    test eax, eax
    jz .Lcheck_tick
    cmp dword ptr [rdi + 8], 0x12  // WM_QUIT
    je .Lquit
    mov rcx, rsi
    mov rdx, rdi
    call_import IsDialogMessageA
    test eax, eax
    jnz .Ldrain
    mov rcx, rdi
    call_import TranslateMessage
    mov rcx, rdi
    call_import DispatchMessageA
.Ldrain:
    dec ebp
    jnz .Lpeek

.Lcheck_tick:
    // Queued stops and restarts invalidate the signal from the earlier timer generation.
    cmp dword ptr [rsp + 40], 1    // WAIT_OBJECT_0 before the increment
    jne .Lagain
    test r12b, 1
    jz .Lagain
    cmp r12d, dword ptr [rip + runState]
    je .Lclick
.Lagain:
    jmp .Lwait

.Lclick:
    // Win64 passes an eight-byte POINT by value. Reuse the now-idle MSG buffer.
    mov rcx, rdi
    call_import GetCursorPos
    test eax, eax
    jz .Lagain
    mov rcx, qword ptr [rdi]
    call_import WindowFromPoint
    mov rcx, rax
    mov edx, 2                     // GA_ROOT
    call_import GetAncestor
    cmp rax, rsi
    je .Lagain
    mov ecx, 2
    lea rdx, [rip + clickPair]
    mov r8d, 40
    call_import SendInput
    cmp eax, 2
    je .Lagain
    dec eax
    jnz .Lclick_failed
    // Release the button if SendInput accepted only the press.
    mov ecx, 1
    lea rdx, [rip + clickPair + 40]
    mov r8d, 40
    call_import SendInput
.Lclick_failed:
    mov rcx, rsi
    lea rdx, [rip + .Lclick_error]
    call Stop
    jmp .Lwait

.Lwait_failed:
    mov rcx, rsi
    xor edx, edx
    call Stop
    mov rcx, rsi
    lea rdx, [rip + PicoTimerError]
    call ShowRunError
    mov rcx, rsi
    call_import DestroyWindow
    jmp .Lfailed
.Lui_failed:
    xor ecx, ecx
    lea rdx, [rip + .Lui_error]
    call ShowRunError
.Lfailed:
    mov esi, 1
    jmp .Lreturn
.Lquit:
    mov esi, dword ptr [rdi + 16]
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
