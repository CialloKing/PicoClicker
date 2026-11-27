.intel_syntax noprefix

.text
.p2align 4
.globl "_DialogProc@16"
.def "_DialogProc@16"
    .scl 2
    .type 32
.endef
"_DialogProc@16":
    mov eax, dword ptr [esp + 8]
    cmp eax, 0x111                 // WM_COMMAND
    je .Lcommand
    cmp eax, 0x312                 // WM_HOTKEY
    je .Lhotkey
    cmp eax, 0x110                 // WM_INITDIALOG
    je .Ldialog_enter
    cmp eax, 0x10                  // WM_CLOSE
    je .Lclose_message
    cmp eax, 2                     // WM_DESTROY
    je .Ldialog_enter
.Lignored:
    xor eax, eax
    ret 16
.Lcommand:
    cmp dword ptr [esp + 12], IDC_TOGGLE  // BN_CLICKED is zero.
    jne .Lignored
    jmp .Ltoggle_message
.Lclose_message:
    xor eax, eax
    jmp .Ldialog_enter
.Lhotkey:
    cmp dword ptr [esp + 12], ID_HOTKEY
    jne .Lignored
    cmp dword ptr [esp + 16], 0x700000  // VK_F1 and no modifiers
    jne .Lignored
.Ltoggle_message:
    mov al, 1
.Ldialog_enter:
    // Win32 can invoke the callback without Run's register state. Establish a fresh context.
    push ebp
    push ebx
    push esi
    push edi
    mov esi, dword ptr [esp + 20]
    mov ebp, offset ___ImageBase
    lea ebx, [ebp + .Lkernel_iat@IMGREL]
    lea edi, [ebp + _picoRunData@IMGREL]
    test al, al
    jz .Lclose
    dec al
    jz .Ltoggle_message_body
    cmp al, 1                     // WM_DESTROY after the decrement
    je .Ldestroyed

    push 0
    push 1                         // BST_CHECKED
    push 0xf1                      // BM_SETCHECK
    push IDC_LEFT
    push esi
    call_import SendDlgItemMessageA
    push 0x70                      // VK_F1
    push 0x4000                    // MOD_NOREPEAT
    push ID_HOTKEY
    push esi
    call_import RegisterHotKey
    load_iat_text .Lbusy
    test eax, eax
    jz .Lsave_idle
    load_iat_text .Lf1
.Lsave_idle:
    mov dword ptr [edi + .Lidle_status], edx
    call .Lstatus
    jmp .Ldialog_leave
.Ltoggle_message_body:
    call .Ltoggle
    jmp .Ldialog_leave
.Ldestroyed:
    push 0
    call_import PostQuitMessage
    jmp .Ldialog_leave
.Lclose:
    xor edx, edx
    call .Lstop
    push esi
    call_import DestroyWindow
.Ldialog_leave:
    pop edi
    pop esi
    pop ebx
    pop ebp
    push 1
    pop eax
    ret 16
// The helpers share ESI (HWND), EDI (state), EBX (IAT) and EBP (module).
.Lstop:
    test byte ptr [edi + .Lgeneration], 1
    jz .Lafter_cancel
    inc dword ptr [edi + .Lgeneration]
    push edx
    push dword ptr [edi + .Ltimer]
    call_import CancelWaitableTimer
    pop edx
.Lafter_cancel:
    test edx, edx
    jne .Lshow_start
    ret

.Lshow_start:
    push edx
    lea edx, [ebp + .Lstart@IMGREL]
    jmp .Ltoggle_label
.Lshow_stop:
    push edx
    lea edx, [ebp + .Lstop_label@IMGREL]
.Ltoggle_label:
    push IDC_TOGGLE
    pop ecx
    call .Lset_text
    pop edx
.Lstatus:
    push IDC_STATUS
    pop ecx
.Lset_text:
    push edx
    push 0
    push 0x0c                      // WM_SETTEXT
    push ecx
    push esi
    call_import SendDlgItemMessageA
    ret

.Lrun_error:
    lea eax, [ebp + .Ltitle@IMGREL]
    push 0x10                      // MB_ICONERROR
    push eax
    push edx
    push ecx
    call_import MessageBoxA
    ret

.Ltoggle:
    test byte ptr [edi + .Lgeneration], 1
    jz .Lread_interval
    mov edx, dword ptr [edi + .Lidle_status]
    jmp .Lstop
.Lrestore_bad_interval:
    pop edi
.Lbad_interval:
    load_iat_text .Linterval
    jmp .Ltoggle_status
.Lread_interval:
    // Each start captures the interval and button; edits only affect the next start.
    sub esp, 12
    // The first eight bytes hold the interval text, then the timer's LARGE_INTEGER.
    mov eax, dword ptr [ebx + .Lcookie_disp]
    xor eax, esp
    mov dword ptr [esp + 8], eax
    push esp                       // PUSH saves the buffer address before moving ESP.
    push 7                         // Read an extra byte to reject a truncated long value.
    push 0x0d                      // WM_GETTEXT
    push IDC_INTERVAL
    push esi
    call_import SendDlgItemMessageA
    dec eax
    cmp eax, 4
    ja .Lbad_interval
    lea ecx, [eax + 1]
    xor eax, eax
    push edi
    lea edi, [esp + 4]
.Ldigit:
    movzx edx, byte ptr [edi]
    sub edx, '0'
    cmp edx, 9
    ja .Lrestore_bad_interval
    imul eax, eax, 10
    add eax, edx
    inc edi
    loop .Ldigit                   // ECX is the validated character count.
    pop edi
    dec eax                        // One unsigned range check rejects both zero and values over 60000.
    cmp eax, 59999
    ja .Lbad_interval
    inc eax
    mov dword ptr [esp], eax
    cmp dword ptr [edi + .Ltimer], 0
    jne .Lstart_timer
    push 0x100002                  // SYNCHRONIZE | TIMER_MODIFY_STATE
    push 2                         // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    push 0
    push 0
    call_import CreateWaitableTimerExA
    mov dword ptr [edi + .Ltimer], eax
    test eax, eax
    jz .Ltimer_failed
.Lstart_timer:
    mov ecx, dword ptr [esp]
    imul eax, ecx, -10000          // A negative due time is relative.
    cdq
    mov dword ptr [esp], eax
    mov dword ptr [esp + 4], edx
    mov edx, esp
    xor eax, eax
    push eax
    push eax
    push eax
    push ecx
    push edx
    push dword ptr [edi + .Ltimer]
    call_import SetWaitableTimer
    test eax, eax
    jz .Ltimer_failed

    push IDC_MIDDLE
    pop ecx
.Lradio:
    push ecx
    push 0
    push 0
    push 0xf0                      // BM_GETCHECK
    push ecx
    push esi
    call_import SendDlgItemMessageA
    pop ecx
    dec eax
    jz .Lchecked_radio
    inc ecx
    cmp ecx, IDC_RIGHT + 1
    jne .Lradio
    push 2                         // MOUSEEVENTF_LEFTDOWN
    pop eax
    jmp .Lready
.Lchecked_radio:
    push 8                         // MOUSEEVENTF_RIGHTDOWN
    pop eax
    cmp ecx, IDC_MIDDLE
    jne .Lready
    shl eax, 2                     // MOUSEEVENTF_MIDDLEDOWN
.Lready:
    mov dword ptr [edi + .Lclick_pair + .Lmouse_flags], eax
    add eax, eax                   // The release flag follows its press flag.
    mov dword ptr [edi + .Lclick_pair + .Linput_size + .Lmouse_flags], eax
    inc dword ptr [edi + .Lgeneration]
    load_iat_text .Lon
    call .Lshow_stop
    jmp .Ltoggle_return
.Ltimer_failed:
    load_iat_text .Ltimer_error
.Ltoggle_status:
    call .Lstatus
.Ltoggle_return:
    mov ecx, dword ptr [esp + 8]
    xor ecx, esp
    call .Lcheck_cookie
    add esp, 12
    ret

.att_syntax prefix
