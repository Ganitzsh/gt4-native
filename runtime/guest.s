# The PS2's main RAM inside the executable image (see runtime/guest.h and tools/game_link.py).
# "bwxs": uninitialized (no file contents), writable, executable (trampolines live at the original
# function addresses), shared (so the uncached mirrors can be mapped as further views of the same
# pages). The link script places this section at 0x00011000, right after the image headers.
        .section .guest,"bwxs"
        .globl  _gt4_guest_ram
_gt4_guest_ram:
        .space  0x02000000 - 0x00011000
