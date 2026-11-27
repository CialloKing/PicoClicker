.macro descriptor dll
    .long 0, 0, 0
    .rva .L\dll\()_name
    .rva .L\dll\()_iat
.endm

.section .idata$2,"dr"
.globl PicoImportDescriptors
PicoImportDescriptors:
    descriptor kernel
    descriptor user
    // LLD appends the null import descriptor.

.macro import name, stdcall_bytes
#ifdef __i386__
    .globl "__imp__\name@\stdcall_bytes"
"__imp__\name@\stdcall_bytes":
#else
    .globl __imp_\name
__imp_\name:
#endif
    .set .Liat_\name, . - .Lkernel_iat
    .rva .Lhint_\name
#ifdef __x86_64__
    .long 0
#endif
    // Reuse the preceding string's terminator while keeping each hint word aligned.
    // A hint may also use the preceding character. Windows checks the full name.
    .section .idata$6,"dr"
    .if (. - .Lhint_begin) % 2
        .set .Lhint_\name, . - 1
        .byte 0
    .else
        .set .Lhint_\name, . - 2
    .endif
    .asciz "\name"
    .section .idata$5,"dr"
.endm

.macro end_iat
    .long 0
#ifdef __x86_64__
    .long 0
#endif
.endm

.section .idata$6,"dr"
.p2align 1
.Lhint_begin:
    .short 0

.section .idata$5,"dr"
#ifdef __x86_64__
.p2align 3
#else
.p2align 2
#endif
