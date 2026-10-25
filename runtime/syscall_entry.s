# Target of a syscall stub with no implementation (kernel_install_syscalls writes `push N; jmp` here).
#   [esp] = N, [esp+4] = the caller's return address: exactly gt4_sys_unimplemented(int, uint32_t)'s
#   arguments once this call pushes its own return address. It does not return.
        .text
        .globl  _gt4_sys_unimplemented_entry
_gt4_sys_unimplemented_entry:
        call    _gt4_sys_unimplemented
        hlt
