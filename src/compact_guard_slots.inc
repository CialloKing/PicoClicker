// Keep the loader-patched guard slots read-only. Older loaders can use these
// leaf defaults without pulling in the CRT's aligned CFG and XFG stubs.
#ifdef __x86_64__
.Lguard_dispatch_fallback:
    jmp rax
#endif
.section .00cfg,"dr"
#ifdef __i386__
.p2align 2
.globl ___guard_check_icall_fptr
___guard_check_icall_fptr:
    .long .Lguard_check_fallback
#else
.p2align 3
.globl __guard_check_icall_fptr, __guard_dispatch_icall_fptr
__guard_check_icall_fptr:
    .quad .Lguard_check_fallback
__guard_dispatch_icall_fptr:
    .quad .Lguard_dispatch_fallback
#endif
.att_syntax prefix
