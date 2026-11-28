.intel_syntax noprefix
.text
.p2align 4
.globl DialogProc
.def DialogProc
    .scl 2
    .type 32
.endef
DialogProc:
    mov eax, edx
    sub eax, 0x110                 // WM_INITDIALOG and WM_COMMAND are consecutive.
    jz .Ldialog_enter
    dec eax
    jz .Lcommand
    cmp edx, 0x312                 // WM_HOTKEY
    je .Lhotkey
    cmp edx, 0x10                  // WM_CLOSE
    je .Lclosing_dialog
    cmp edx, 2                     // WM_DESTROY
    je .Lclosing_dialog
.Lignored_message:
    xor eax, eax
    ret
.Lclosing_dialog:
    // ExitProcess releases the window and timer after the loop sees the flag.
    mov byte ptr [rip + picoRunData + .Lclosing], 1
    jmp .Lhandled_leaf
.Lcommand:
    lea eax, [r8 - IDC_LEFT]
    cmp eax, IDC_TOGGLE - IDC_LEFT // BN_CLICKED is zero.
    je .Ltoggle_message
    ja .Lignored_message
    // Record the selection separately so an active click pair remains stable.
    mov byte ptr [rip + picoRunData + .Lselected_button], al
.Lhandled_leaf:
    xor eax, eax
    inc al
    ret
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
    lea rbx, [rip + __imp_GetAncestor]
    access_iat_cookie 0x8b, 0x43   // MOV RAX, [RBX+disp8]
    xor rax, rsp
    mov qword ptr [rsp + 56], rax
    mov rsi, rcx
    lea rdi, [rip + picoRunData]

    cmp dl, 1
    je .Ltoggle

    xor eax, eax
    mov qword ptr [rsp + 32], rax
    lea edx, [rax + IDC_LEFT]
    mov r8d, 0xf1                  // BM_SETCHECK
    lea r9d, [rax + 1]             // BST_CHECKED
    call_import SendDlgItemMessageA
    mov rcx, rsi
    xor edx, edx
    mov dl, ID_HOTKEY
    mov r8d, 0x4000                // MOD_NOREPEAT
    lea r9d, [rdx + 0x70 - ID_HOTKEY] // VK_F1
    call_import RegisterHotKey
    load_iat_text .Lbusy
    test eax, eax
    jz .Lsave_idle
    load_iat_text .Lf1
.Lsave_idle:
    mov qword ptr [rdi + .Lidle_status], rdx
    jmp .Ltoggle_status

.Ltoggle:
    test byte ptr [rdi + .Lgeneration], 1
    jz .Lread_interval
    call .Lcancel_timer
    call .Lshow_start
    mov rdx, qword ptr [rdi + .Lidle_status]
.Ltoggle_status:
    call .Lstatus
.Lhandled_message:
    mov rcx, qword ptr [rsp + 56]
    xor rcx, rsp
    call .Lcheck_cookie
    lea eax, [rcx + 1]             // The Win64 cookie check returns with RCX zero.
    add rsp, 64
    pop rdi
    pop rsi
    pop rbx
    ret
.Lbad_interval:
    load_iat_text .Linterval
    jmp .Ltoggle_status
.Lread_interval:
    // Each start captures the interval and button; edits only affect the next start.
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
    lea ecx, [rax + 1]
    lea r8, [rsp + 48]
    xor eax, eax
.Ldigit:
    movzx edx, byte ptr [r8]
    sub edx, '0'
    cmp edx, 9
    ja .Lbad_interval
    imul eax, eax, 10
    add eax, edx
    inc r8
    loop .Ldigit                   // RCX is the validated character count.
    dec eax
    cmp eax, 59999
    ja .Lbad_interval
    inc eax
    mov dword ptr [rsp + 48], eax
    mov rcx, qword ptr [rdi + .Ltimer]
    test rcx, rcx
    jnz .Larm_timer
    xor edx, edx
    lea r8d, [rdx + 2]             // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
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
    xorps xmm0, xmm0
    movaps xmmword ptr [rsp + 32], xmm0  // Both stack arguments are zero.
    call_import SetWaitableTimer
    test eax, eax
    jz .Ltimer_failed

    movzx eax, byte ptr [rdi + .Lselected_button]
    load_button_flags
    mov dword ptr [rdi + .Lclick_pair + .Lmouse_flags], eax
    add eax, eax                   // Each release flag follows its press flag.
    mov dword ptr [rdi + .Lclick_pair + .Linput_size + .Lmouse_flags], eax
    inc dword ptr [rdi + .Lgeneration]
    call .Lshow_stop
    load_iat_text .Lon
    jmp .Lreport_start
.Ltimer_failed:
    load_iat_text .Ltimer_error
.Lreport_start:
    jmp .Ltoggle_status
.seh_endproc

// Advance the generation before cancelling so a pending signal is discarded.
.Lcancel_timer:
    test byte ptr [rdi + .Lgeneration], 1
    jz .Lcancel_return
    inc dword ptr [rdi + .Lgeneration]
    mov rcx, qword ptr [rdi + .Ltimer]
    tail_import CancelWaitableTimer
.Lcancel_return:
    ret

// Callers know which transition occurred. The leaf helpers share their Win64
// argument area and let the API return directly to the caller.
.Lshow_start:
    lea rdx, [rip + .Lstart]
    jmp .Ltoggle_label
.Lshow_stop:
    lea rdx, [rip + .Lstop_label]
.Ltoggle_label:
    mov al, IDC_TOGGLE
    jmp .Lset_text

// These leaf helpers reuse the caller's shadow space and fifth argument slot.
.Lstatus:
    mov al, IDC_STATUS
.Lset_text:
    mov qword ptr [rsp + 40], rdx
    movzx edx, al
    xor r9d, r9d
    lea r8d, [r9 + 0x0c]          // WM_SETTEXT
    mov rcx, rsi
    tail_import SendDlgItemMessageA

.att_syntax prefix
