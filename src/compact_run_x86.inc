#include "resource.h"

.intel_syntax noprefix

// EBX addresses the compact IAT, whose x86 entries all fit in a signed-byte displacement.
.macro call_import name, bytes
    call dword ptr [ebx + .Liat_\name]
.endm

// COFF x86 address operands need the imm32 form of PUSH.
.macro push_address address
    .byte 0x68
    .long \address
.endm

.section .rdata,"dr"
.Lui_error:
    .asciz "UI Err"
.Lclick_error:
    .asciz "Click Err"

.text
.globl "@Run@4"
.def "@Run@4"
    .scl 2
    .type 32
.endef
"@Run@4":
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 40
    // Two DWORDs, MSG, then the same stack-cookie encoding used by clang-cl.
    mov eax, dword ptr [___security_cookie]
    xor eax, esp
    mov dword ptr [esp + 36], eax
    mov ebx, offset .Lkernel_iat
    push 0
    push_address "_DialogProc@16"
    push 0
    push IDD_MAIN
    push ecx
    call_import CreateDialogParamA, 20
    test eax, eax
    jz .Lui_failed
    mov esi, eax
    lea edi, [esp + 8]

.Lwait:
    mov eax, dword ptr [_runState]
    mov dword ptr [esp], eax
    and eax, 1
    push 4                         // MWMO_INPUTAVAILABLE
    push 0x1cff                    // QS_ALLINPUT
    push -1                        // INFINITE
    push_address _timer
    push eax
    call_import MsgWaitForMultipleObjectsEx, 20
    inc eax
    jz .Lwait_failed               // WAIT_FAILED was -1
    mov dword ptr [esp + 4], eax
    push 32
    pop ebp
.Lpeek:
    push 1                         // PM_REMOVE
    xor eax, eax
    push eax
    push eax
    push eax
    push edi
    call_import PeekMessageA, 20
    test eax, eax
    jz .Lcheck_tick
    cmp dword ptr [edi + 4], 0x12  // WM_QUIT
    je .Lquit
    push edi
    push esi
    call_import IsDialogMessageA, 8
    test eax, eax
    jnz .Ldrain
    push edi
    call_import TranslateMessage, 4
    push edi
    call_import DispatchMessageA, 4
.Ldrain:
    dec ebp
    jnz .Lpeek

.Lcheck_tick:
    // A stop or restart invalidates the signal from the earlier timer generation.
    cmp dword ptr [esp + 4], 1     // WAIT_OBJECT_0 before the increment
    jne .Lagain
    mov eax, dword ptr [esp]
    test al, 1
    jz .Lagain
    cmp eax, dword ptr [_runState]
    je .Lclick
.Lagain:
    jmp .Lwait

.Lclick:
    // The message queue is drained. Reuse its buffer for the POINT passed to Win32.
    push edi
    call_import GetCursorPos, 4
    test eax, eax
    jz .Lagain
    push dword ptr [edi + 4]
    push dword ptr [edi]
    call_import WindowFromPoint, 8
    push 2                         // GA_ROOT
    push eax
    call_import GetAncestor, 8
    // A press over the clicker's own window must not toggle its controls.
    cmp eax, esi
    je .Lagain
    push 28
    push_address _clickPair
    push 2
    call_import SendInput, 12
    cmp eax, 2
    je .Lagain
    dec eax
    jnz .Lclick_failed
    // Release the button if SendInput accepted only the press.
    push 28
    push_address _clickPair+28
    push 1
    call_import SendInput, 12
.Lclick_failed:
    mov ecx, esi
    mov edx, offset .Lclick_error
    call "@Stop@8"
    jmp .Lwait

.Lwait_failed:
    mov ecx, esi
    xor edx, edx
    call "@Stop@8"
    mov ecx, esi
    mov edx, offset _PicoTimerError
    call "@ShowRunError@8"
    push esi
    call_import DestroyWindow, 4
    jmp .Lfailed
.Lui_failed:
    xor ecx, ecx
    mov edx, offset .Lui_error
    call "@ShowRunError@8"
.Lfailed:
    push 1
    pop esi
    jmp .Lreturn
.Lquit:
    mov esi, dword ptr [edi + 8]
.Lreturn:
    mov ecx, dword ptr [esp + 36]
    xor ecx, esp
    call "@__security_check_cookie@4"
    mov eax, esi
    add esp, 40
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.att_syntax prefix
