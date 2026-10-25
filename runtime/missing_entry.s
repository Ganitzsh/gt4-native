# Entered by `call gt4_missing_entry` from the trampoline of a function that has no native code.
#   [esp]   = trampoline address + 5 (so the function's PS2 address is [esp] - 5)
#   [esp+4] = the caller's return address
# gt4_missing(fn, caller) reports the call and either stops the process or returns; on return the
# missing function "returns 0": eax = edx = 0, back to the caller (cdecl: the caller pops arguments).
        .text
        .globl  _gt4_missing_entry
_gt4_missing_entry:
        pushl   %ebp
        movl    %esp, %ebp
        pushl   %ecx
        movl    8(%ebp), %eax           # caller return address
        movl    4(%ebp), %ecx           # trampoline + 5
        subl    $5, %ecx
        pushl   %eax
        pushl   %ecx
        call    _gt4_missing
        addl    $8, %esp
        popl    %ecx
        popl    %ebp
        addl    $4, %esp                # drop the trampoline's return address
        xorl    %eax, %eax
        xorl    %edx, %edx
        ret
