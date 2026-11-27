#include "compact_cookie.h"
#include "resource.h"

.intel_syntax noprefix

.set .Lgeneration, 0
.set .Ltimer, 4
.set .Lidle_status, 8
.set .Lclick_pair, 12
.set .Lmouse_flags, 16
.set .Linput_size, 28
.set .Lselected_button, 68

// The dialog manager calls this address, which the assembly builds from its RVA.
.section .gfids$y,"dr"
.symidx "_DialogProc@16"

.text
.globl "_PicoClickerEntry@0"
.def "_PicoClickerEntry@0"
    .scl 2
    .type 32
.endef
"_PicoClickerEntry@0":
    // The entry and message loop share their module and IAT bases through ExitProcess.
    mov ebp, offset ___ImageBase
    lea ebx, [ebp + .Lkernel_iat@IMGREL]
    mov esi, dword ptr [ebx + .Lcookie_disp]
    test esi, esi
    jz .Lbad_cookie
    cmp esi, INITIAL_COOKIE
    jne .Linitialize
.Lbad_cookie:
    push 6                         // FAST_FAIL_GS_COOKIE_INIT
    pop ecx
    int 0x29
.Linitialize:
    push -4                        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    call_import SetProcessDpiAwarenessContext
    sub esp, 44
    // Timer generation and drain counter, then MSG, the /GS cookie and HWND.
    xor esi, esp
    mov dword ptr [esp + 36], esi
    lea edx, [ebp + .Ldialog_template@IMGREL]
    lea esi, [ebp + .Ldialog_encoding@IMGREL]
    mov edi, edx
    decode_dialog
    lea eax, [ebp + "_DialogProc@16"@IMGREL]
    push 0
    push eax
    push 0
    push edx
    push ebp
    call_import CreateDialogIndirectParamA
    mov esi, eax
    test eax, eax
    jz .Lui_failed
    mov dword ptr [esp + 40], esi
    lea esi, [esp + 8]
    lea edi, [ebp + _picoRunData@IMGREL]

.Lwait:
    mov eax, dword ptr [edi + .Lgeneration]
    mov dword ptr [esp], eax
    and eax, 1
    push 4                         // MWMO_INPUTAVAILABLE
    push 0x1cff                    // QS_ALLINPUT
    push -1                        // INFINITE
    lea edx, [edi + .Ltimer]
    push edx
    push eax
    call_import MsgWaitForMultipleObjectsEx
    inc eax
    jz .Lwait_failed               // WAIT_FAILED was -1
    dec eax
    jz .Lstart_drain               // WAIT_OBJECT_0
    // Clear the snapshot's active bit when this wait did not signal the timer.
    and byte ptr [esp], -2
.Lstart_drain:
    mov byte ptr [esp + 4], 32
.Lpeek:
    push 1                         // PM_REMOVE
    xor eax, eax
    push eax
    push eax
    push eax
    push esi
    call_import PeekMessageA
    test eax, eax
    jz .Lcheck_tick
    cmp dword ptr [esi + 4], 0x12  // WM_QUIT
    je .Lquit
    // Translate printable keys for the edit control without dialog navigation.
    push esi
    call_import TranslateMessage
    push esi
    call_import DispatchMessageA
    dec byte ptr [esp + 4]
    jnz .Lpeek

.Lcheck_tick:
    // A stop or restart invalidates the signal from the earlier timer generation.
    mov eax, dword ptr [esp]
    test al, 1
    jz .Lagain
    cmp eax, dword ptr [edi + .Lgeneration]
    je .Lclick
.Lagain:
    jmp .Lwait

.Lclick:
    // The message queue is drained. Reuse its buffer for the POINT passed to Win32.
    push esi
    call_import GetCursorPos
    test eax, eax
    jz .Lagain
    push dword ptr [esi + 4]
    push dword ptr [esi]
    call_import WindowFromPoint
    push 2                         // GA_ROOT
    push eax
    call_import GetAncestor
    // A press over the clicker's own window must not toggle its controls.
    cmp eax, dword ptr [esp + 40]
    je .Lagain
    lea eax, [edi + .Lclick_pair]
    push .Linput_size
    push eax
    push 2
    call_import SendInput
    // Two submitted events give a count of 0, 1 or 2. Only 1 sets CF here.
    shr eax, 1
    jnz .Lagain
    jnc .Lclick_failed
    // Release the button if SendInput accepted only the press.
    lea eax, [edi + .Lclick_pair + .Linput_size]
    push .Linput_size
    push eax
    push 1
    call_import SendInput
.Lclick_failed:
    push esi
    mov esi, dword ptr [esp + 44]  // The dialog helpers expect HWND in ESI.
    load_iat_text .Lclick_error
    call .Lstop
    pop esi
    jmp .Lagain

.Lwait_failed:
    mov esi, dword ptr [esp + 40]
    xor edx, edx
    call .Lstop
    load_iat_text .Ltimer_error
    jmp .Lshow_error
.Lui_failed:
    load_iat_text .Lui_error
.Lshow_error:
    mov ecx, esi
    call .Lrun_error
    test esi, esi                  // Dialog creation can fail before there is a window.
    jz .Lfailed
    push esi
    call_import DestroyWindow
.Lfailed:
    push 1
    pop esi
    jmp .Lreturn
.Lquit:
    mov esi, dword ptr [esi + 8]
.Lreturn:
    mov ecx, dword ptr [esp + 36]
    xor ecx, esp
    call .Lcheck_cookie
    push esi
    call_import ExitProcess
    int3

.att_syntax prefix
