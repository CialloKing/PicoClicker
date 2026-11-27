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
    je .Lclosing_dialog
    cmp eax, 2                     // WM_DESTROY
    je .Lclosing_dialog
.Lignored:
    xor eax, eax
    ret 16
.Lclosing_dialog:
    // The loop exits before another click. ExitProcess tears down the window,
    // hotkey and timer together after the callback returns.
    mov byte ptr [_picoRunData + .Lclosing], 1
    jmp .Lhandled
.Lcommand:
    mov eax, dword ptr [esp + 12]
    sub eax, IDC_LEFT
    cmp eax, IDC_TOGGLE - IDC_LEFT  // BN_CLICKED is zero.
    je .Ltoggle_message
    ja .Lignored
    // The active click pair continues to use the selection captured at start.
    mov byte ptr [_picoRunData + .Lselected_button], al
.Lhandled:
    push 1
    pop eax
    ret 16
.Lhotkey:
    cmp dword ptr [esp + 12], ID_HOTKEY
    jne .Lignored
    cmp dword ptr [esp + 16], 0x700000  // VK_F1 and no modifiers
    jne .Lignored
.Ltoggle_message:
    mov al, 1
.Ldialog_enter:
    // Win32 can invoke the callback without Run's register state. Establish a fresh context.
    pushad
    mov esi, dword ptr [esp + 36]
    mov ebx, offset .Lkernel_iat
    mov edi, offset _picoRunData
    cmp al, 1
    je .Ltoggle_message_body

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
.Ldialog_leave:
    popad
    push 1
    pop eax
    ret 16
// The helpers share ESI (HWND), EDI (state) and EBX (IAT).
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
    mov edx, offset .Lstart
    jmp .Ltoggle_label
.Lshow_stop:
    push edx
    mov edx, offset .Lstop_label
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

.Ltoggle:
    test byte ptr [edi + .Lgeneration], 1
    jz .Lread_interval
    mov edx, dword ptr [edi + .Lidle_status]
    jmp .Lstop
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
    mov ebp, esp
.Ldigit:
    movzx edx, byte ptr [ebp]
    sub edx, '0'
    cmp edx, 9
    ja .Lbad_interval
    imul eax, eax, 10
    add eax, edx
    inc ebp
    loop .Ldigit                   // ECX is the validated character count.
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
    push edx
    push eax
    mov edx, esp
    xor eax, eax
    push eax
    push eax
    push eax
    push ecx
    push edx
    push dword ptr [edi + .Ltimer]
    call_import SetWaitableTimer
    add esp, 8
    test eax, eax
    jz .Ltimer_failed

    movzx eax, byte ptr [edi + .Lselected_button]
    // MOVZX EAX,[EBX+EAX+disp8], with the read-only flags next to the IAT.
    .byte 0x0f, 0xb6, 0x44, 0x03, .Lbutton_disp
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
