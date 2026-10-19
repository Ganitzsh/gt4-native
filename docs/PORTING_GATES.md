# Native port: technical gates

## Gate 1: guest address translation (implemented)
- PS2 guest addresses are 32-bit integers, not host pointers.
- Checked little-endian byte/word/dword access via `gt4::guest::MemoryView`.
- Explicitly reject out-of-bounds and 32-bit address-space overflow.
- Does **not** emulate MIPS unaligned load/store instructions, memory-mapped IO, DMA, cache behavior, or VU memory.

## Gate 2: host/guest ABI (one reviewed object slice implemented)
- Minimum wire extent and signed fields 0x174..0x188 validated for one original accessor family; complete class ownership remains unknown.
- Build a vtable dispatch adapter with function-address resolution.
- Same-object argument flow verified from the original 005C9970 -> 001465D8 call site. Other calling conventions and runtime traces remain pending.

## Gate 3: first real upstream functions (narrow integration implemented)
- Fifteen original source functions selected; thirteen adapted bodies linked into the production executable, with guest-backed accessor-chain demonstration.
- Pin the upstream git revision; record file path and license.
- Compile with a modern host compiler, compare outputs with the original where possible.
- `gt4_native --object-demo` calls original setters and the normalized reader, which calls the original divisor body on the same guest-backed view. See history/MILESTONE_4.md for scope and limitations.

## Gate 4: platform services (pending)
- Filesystem and VOL, allocators, timing/input, renderer, sound, scripting VM.
- No proprietary game assets in repository or CI.
