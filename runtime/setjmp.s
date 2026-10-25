# setjmp/longjmp of the image (newlib, 005A58D8 / 005A5930), for the game's own uses (004AE0B8,
# 004B0120...). Reached by `jmp` from the trampoline, so [esp] is the caller's return address.
# Buffer: ebx, esi, edi, ebp, esp (after return), eip. The game's jmp_buf (MIPS) is larger.
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
        xorl    %eax, %eax
        ret

        .globl  _gt4_replace_longjmp
_gt4_replace_longjmp:
        movl    4(%esp), %edx
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
