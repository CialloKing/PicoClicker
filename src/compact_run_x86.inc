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
.set .Lclosing, 69

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
    sub esp, 40
    // Timer generation and drain counter, then MSG and the /GS cookie.
    xor esi, esp
    mov dword ptr [esp + 36], esi
#ifdef PICOCLICKER_WIDE_DIALOG
    xor ecx, ecx
    mov edx, IDD_MAIN
#else
    lea edx, [ebp + .Ldialog_template@IMGREL]
    lea esi, [ebp + .Ldialog_encoding@IMGREL]
    mov edi, edx
    decode_dialog                  // Leaves ECX zero for the parameter and parent.
#endif
    lea eax, [ebp + "_DialogProc@16"@IMGREL]
    push ecx
    push eax
    push ecx
    push edx
    push ebp
    call_import CREATE_DIALOG_API
    mov esi, eax
    test eax, eax
    jz .Lui_failed
    // After dialog creation EBP can hold MSG and leave HWND in the helper ABI.
    lea ebp, [esp + 8]
    mov edi, offset _picoRunData

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
    push ebp
    call_import UI_API(PeekMessage)
    test eax, eax
    jz .Lcheck_tick
    cmp dword ptr [ebp + 4], 0x12  // WM_QUIT
    je .Lquit
#ifdef PICOCLICKER_WIDE_DIALOG
    push ebp
    push esi
    call_import IsDialogMessageW
    test eax, eax
    jnz .Lnext_message
#endif
    push ebp
    call_import TranslateMessage
    push ebp
    call_import UI_API(DispatchMessage)
.Lnext_message:
    dec byte ptr [esp + 4]
    jnz .Lpeek

.Lcheck_tick:
    cmp byte ptr [edi + .Lclosing], 0
    jne .Lclosed
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
    push ebp
    call_import GetCursorPos
    test eax, eax
    jz .Lagain
    push dword ptr [ebp + 4]
    push dword ptr [ebp]
    call_import WindowFromPoint
    push 2                         // GA_ROOT
    push eax
    call_import GetAncestor
    // A press over the clicker's own window must not toggle its controls.
    cmp eax, esi
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
    load_status_text click_error
    call .Lstop
    jmp .Lagain

.Lwait_failed:
    xor edx, edx
    call .Lstop
    load_status_text wait_error
    jmp .Lshow_error
.Lui_failed:
    load_status_text ui_error
.Lshow_error:
    mov eax, offset DIALOG_TITLE
    push 0x10                      // MB_ICONERROR
    push eax
    push edx
    push esi
    call_import UI_API(MessageBox)        // ExitProcess releases the dialog after the error.
    push 1
    pop esi
    jmp .Lreturn
.Lclosed:
    xor esi, esi
    jmp .Lreturn
.Lquit:
    mov esi, dword ptr [ebp + 8]
.Lreturn:
    mov ecx, dword ptr [esp + 36]
    xor ecx, esp
    call .Lcheck_cookie
    push esi
    call_import ExitProcess
    int3

.att_syntax prefix
