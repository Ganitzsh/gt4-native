# Native port: technical gates

## Gate 1: guest address translation (implemented)
- PS2 guest addresses are 32-bit integers, not host pointers.
- Checked little-endian byte/word/dword access via `gt4::guest::MemoryView`.
- Explicitly reject out-of-bounds and 32-bit address-space overflow.
- Does **not** emulate MIPS unaligned load/store instructions, memory-mapped IO, DMA, cache behavior, or VU memory.

## Gate 2: host/guest ABI (pending)
- Validate layout and endianness of representative real upstream structures.
- Build a vtable dispatch adapter with function-address resolution.
- Confirm old GCC C++ ABI assumptions against matching source and runtime traces.

## Gate 3: first real upstream function (pending)
- Choose a leaf function without PS2 hardware dependencies.
- Pin the upstream git revision; record file path and license.
- Compile with a modern host compiler, compare outputs with the original where possible.
- Do not claim runtime integration until the native binary actually calls that function.

## Gate 4: platform services (pending)
- Filesystem and VOL, allocators, timing/input, renderer, sound, scripting VM.
- No proprietary game assets in repository or CI.
