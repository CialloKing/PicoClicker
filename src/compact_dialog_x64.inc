.intel_syntax noprefix
.text
.p2align 4
.globl DialogProc
.def DialogProc
    .scl 2
    .type 32
.endef
DialogProc:
    cmp edx, 0x111                 // WM_COMMAND
    je .Lcommand
    cmp edx, 0x312                 // WM_HOTKEY
    je .Lhotkey
    cmp edx, 0x110                 // WM_INITDIALOG
    je .Ldialog_enter
    cmp edx, 0x10                  // WM_CLOSE
    je .Lclose_message
    cmp edx, 2                     // WM_DESTROY
    je .Ldialog_enter
.Lignored_message:
    xor eax, eax
    ret
.Lcommand:
    cmp r8d, IDC_TOGGLE            // BN_CLICKED is zero.
    je .Ltoggle_message
    cmp r8w, 2                     // IDCANCEL ignores the notification code.
    jne .Lignored_message
.Lclose_message:
    xor edx, edx
    jmp .Ldialog_enter
.Lhotkey:
    cmp r8, ID_HOTKEY
    jne .Lignored_message
    cmp r9, 0x700000               // VK_F1 and no modifiers
    jne .Lignored_message
.Ltoggle_message:
    mov dl, 1

// Only handled messages enter the unwindable frame, with DL selecting the action.
.seh_proc .Ldialog_enter
.Ldialog_enter:
    push rbx
    .seh_pushreg rbx
    push rsi
    .seh_pushreg rsi
    push rdi
    .seh_pushreg rdi
    sub rsp, 64
    .seh_stackalloc 64
    .seh_endprologue

    // The outgoing argument area ends at 48; the edit buffer and cookie follow it.
    mov rax, qword ptr [rip + __security_cookie]
    xor rax, rsp
    mov qword ptr [rsp + 56], rax
    mov rsi, rcx
    lea rbx, [rip + __imp_GetAncestor]
    lea rdi, [rip + picoRunData]

    test dl, dl
    jz .Lclose_dialog
    dec dl
    jz .Ltoggle
    cmp dl, 1                      // WM_DESTROY after the decrement.
    je .Ldestroy_dialog

    xor eax, eax
    mov qword ptr [rsp + 32], rax
    mov edx, IDC_LEFT
    mov r8d, 0xf1                  // BM_SETCHECK
    lea r9d, [rax + 1]             // BST_CHECKED
    call_import SendDlgItemMessageA
    mov rcx, rsi
    mov edx, ID_HOTKEY
    mov r8d, 0x4000                // MOD_NOREPEAT
    mov r9d, 0x70                  // VK_F1
    call_import RegisterHotKey
    lea rdx, [rip + .Lbusy]
    test eax, eax
    jz .Lsave_idle
    lea rdx, [rip + .Lf1]
.Lsave_idle:
    mov qword ptr [rdi + .Lidle_status], rdx
    jmp .Ltoggle_status
.Ldestroy_dialog:
    xor ecx, ecx
    call_import PostQuitMessage
    jmp .Lhandled_message
.Lclose_dialog:
    xor edx, edx
    mov al, 1
    call .Lupdate_state
    mov rcx, rsi
    call_import DestroyWindow
    jmp .Lhandled_message

.Ltoggle:
    test byte ptr [rdi + .Lgeneration], 1
    jz .Lread_interval
    mov rdx, qword ptr [rdi + .Lidle_status]
    mov al, 1
    call .Lupdate_state
    jmp .Lhandled_message
.Lbad_interval:
    lea rdx, [rip + .Linterval]
    jmp .Ltoggle_status
.Lread_interval:
    lea r9, [rsp + 48]
    mov qword ptr [rsp + 32], r9
    mov rcx, rsi
    mov edx, IDC_INTERVAL
    lea r8d, [rdx + 0x0d - IDC_INTERVAL]  // WM_GETTEXT
    lea r9d, [rdx + 7 - IDC_INTERVAL]     // Read an extra byte to detect truncation.
    call_import SendDlgItemMessageA
    dec eax
    cmp eax, 4
    ja .Lbad_interval
    lea r8d, [rax + 1]
    lea rcx, [rsp + 48]
    xor eax, eax
.Ldigit:
    movzx edx, byte ptr [rcx]
    sub edx, '0'
    cmp edx, 9
    ja .Lbad_interval
    imul eax, eax, 10
    add eax, edx
    inc rcx
    dec r8d
    jnz .Ldigit
    dec eax
    cmp eax, 59999
    ja .Lbad_interval
    inc eax
    mov dword ptr [rsp + 48], eax
    mov rcx, qword ptr [rdi + .Ltimer]
    test rcx, rcx
    jnz .Larm_timer
    xor edx, edx
    mov r8d, 2                     // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    mov r9d, 0x100002              // SYNCHRONIZE | TIMER_MODIFY_STATE
    call_import CreateWaitableTimerExA
    mov qword ptr [rdi + .Ltimer], rax
    mov rcx, rax
    test rax, rax
    jz .Ltimer_failed
.Larm_timer:
    mov r8d, dword ptr [rsp + 48]
    imul eax, r8d, -10000          // Relative due time in units of 100 ns.
    cdqe
    mov qword ptr [rsp + 48], rax
    lea rdx, [rsp + 48]
    xor r9d, r9d
    mov qword ptr [rsp + 32], r9
    mov qword ptr [rsp + 40], r9
    call_import SetWaitableTimer
    test eax, eax
    jz .Ltimer_failed

    mov edx, IDC_MIDDLE
    call .Lget_check
    dec eax                       // BST_CHECKED is 1.
    jz .Lmiddle_down
    mov edx, IDC_RIGHT
    call .Lget_check
    dec eax
    mov eax, 2                     // MOUSEEVENTF_LEFTDOWN
    jnz .Lsave_click_pair
    mov al, 8                      // MOUSEEVENTF_RIGHTDOWN
    jmp .Lsave_click_pair
.Lmiddle_down:
    mov al, 32                     // MOUSEEVENTF_MIDDLEDOWN, EAX was zero after DEC.
.Lsave_click_pair:
    mov dword ptr [rdi + .Lclick_pair + .Lmouse_flags], eax
    add eax, eax                   // Each release flag follows its press flag.
    mov dword ptr [rdi + .Lclick_pair + .Linput_size + .Lmouse_flags], eax
    inc dword ptr [rdi + .Lgeneration]
    lea rdx, [rip + .Lon]
    xor eax, eax
    call .Lupdate_state
    jmp .Lhandled_message
.Ltimer_failed:
    lea rdx, [rip + .Ltimer_error]
.Ltoggle_status:
    call .Lstatus
.Lhandled_message:
    mov rcx, qword ptr [rsp + 56]
    xor rcx, rsp
    call .Lcheck_cookie
    mov eax, 1
    add rsp, 64
    pop rdi
    pop rsi
    pop rbx
    ret
.seh_endproc

// Run and DialogProc share RBX (IAT), RSI (dialog) and RDI (state). AL requests
// cancellation; RDX is the status to display, or null for cancellation alone.
.seh_proc .Lupdate_state
.Lupdate_state:
    push rbp
    .seh_pushreg rbp
    sub rsp, 48
    .seh_stackalloc 48
    .seh_endprologue
    mov qword ptr [rsp + 40], rdx
    test al, al
    jz .Lupdate_controls
    test byte ptr [rdi + .Lgeneration], 1
    jz .Lafter_cancel
    inc dword ptr [rdi + .Lgeneration]
    mov rcx, qword ptr [rdi + .Ltimer]
    call_import CancelWaitableTimer
.Lafter_cancel:
    cmp qword ptr [rsp + 40], 0
    je .Lstate_return
.Lupdate_controls:
    mov ebp, IDC_INTERVAL
.Lenable:
    mov rcx, rsi
    mov edx, ebp
    call_import GetDlgItem
    mov rcx, rax
    mov edx, dword ptr [rdi + .Lgeneration]
    not edx
    and edx, 1
    call_import EnableWindow
    inc ebp
    cmp ebp, IDC_RIGHT + 1
    jne .Lenable
    lea rdx, [rip + .Lstart]
    test byte ptr [rdi + .Lgeneration], 1
    jz .Ltoggle_label
    lea rdx, [rip + .Lstop_label]
.Ltoggle_label:
    mov al, IDC_TOGGLE
    call .Lset_text
    mov rdx, qword ptr [rsp + 40]
    call .Lstatus
.Lstate_return:
    add rsp, 48
    pop rbp
    ret
.seh_endproc

// These leaf helpers reuse the caller's shadow space and fifth argument slot.
.Lstatus:
    mov al, IDC_STATUS
.Lset_text:
    mov qword ptr [rsp + 40], rdx
    movzx edx, al
    mov r8d, 0x0c                  // WM_SETTEXT
    xor r9d, r9d
    mov rcx, rsi
    tail_import SendDlgItemMessageA
.Lget_check:
    xor r9d, r9d
    mov qword ptr [rsp + 40], r9
    mov r8d, 0xf0                  // BM_GETCHECK
    mov rcx, rsi
    tail_import SendDlgItemMessageA
.Lrun_error:
    lea r8, [rip + .Ltitle]
    mov r9d, 0x10                  // MB_ICONERROR
    tail_import MessageBoxA

.att_syntax prefix
