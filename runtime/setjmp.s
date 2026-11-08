# setjmp/longjmp of the image (newlib, 005A58D8 / 005A5930), for the game's own uses (004AE0B8,
# 004B0120...). Reached by `jmp` from the trampoline, so [esp] is the caller's return address.
# Buffer: ebx, esi, edi, ebp, esp (after return), eip, then the marker 0x504D4A4E ("NJMP"): the game's
# jmp_buf (MIPS) is larger. A buffer without the marker was filled by the original code's setjmp (GT4_ORIG:
# translated code saves EE registers there); longjmp refuses it (gt4_longjmp_refused) rather than load EE
# values into x86 registers and jump to them. tools/eeref/orig_bridge.cpp checks the marker too.
        .text
        .globl  _gt4_replace_setjmp
_gt4_replace_setjmp:
        movl    4(%esp), %eax
        movl    %ebx, 0(%eax)
        movl    %esi, 4(%eax)
        movl    %edi, 8(%eax)
        movl    %ebp, 12(%eax)
        leal    4(%esp), %ecx
        movl    %ecx, 16(%eax)
        movl    (%esp), %ecx
        movl    %ecx, 20(%eax)
        movl    $0x504D4A4E, 24(%eax)
        xorl    %eax, %eax
        ret

        .globl  _gt4_replace_longjmp
_gt4_replace_longjmp:
        movl    4(%esp), %edx
        cmpl    $0x504D4A4E, 24(%edx)
        jne     2f
        movl    8(%esp), %eax
        testl   %eax, %eax
        jnz     1f
        incl    %eax
1:      movl    0(%edx), %ebx
        movl    4(%edx), %esi
        movl    8(%edx), %edi
        movl    12(%edx), %ebp
        movl    16(%edx), %esp
        jmp     *20(%edx)
2:      pushl   %edx                    # never returns
        call    _gt4_longjmp_refused
