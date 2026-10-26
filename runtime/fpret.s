# libgcc's 64-bit integer to float conversions for game code. Game code takes float and double
# results in eax / eax:edx (tools/game_cc.py: -mno-fp-ret-in-387), the host libgcc returns them
# on the x87 stack, so game objects call these copies instead (tools/game_link.py renames their
# references to gt4g___floatdisf...).
        .text
        .globl  _gt4g___floatdisf
_gt4g___floatdisf:
        fildq   4(%esp)
        subl    $4, %esp
        fstps   (%esp)
        popl    %eax
        ret

        .globl  _gt4g___floatdidf
_gt4g___floatdidf:
        fildq   4(%esp)
        subl    $8, %esp
        fstpl   (%esp)
        popl    %eax
        popl    %edx
        ret
