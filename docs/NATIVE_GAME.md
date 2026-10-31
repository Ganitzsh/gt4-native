# The native game (milestone G6)

`gt4_game.exe` runs the game's own code, compiled from the matched decompilation, natively on
Windows. A function that has no native code yet stops the game with a log of its address.

## How it fits together

```
gt4_host.exe  (64-bit, MSVC)                     gt4_game.exe  (32-bit, MinGW i686)
  SDL3 window, input, audio                        the game's code: ~30,000 decomp sources
  Vulkan display pipeline (docs/RENDERING.md)      compiled by gcc 2.9 retargeted to i386
  GS / VIF1 / VU1 back end (docs/GS_BACKEND.md)    (docs/GAME_TOOLCHAIN.md)
          ^                                        EE kernel, IOP services (runtime/)
          |   shared memory link                            |
          +---- include/gt4/link/protocol.h  <--------------+
                graphics stream, audio ->   <- pad, GS events
```

### The PS2 memory map, natively

- The process is 32-bit, so a PS2 pointer is a host pointer: the game's code and data, its
  objects and the original image's data all keep their addresses.
- The executable's first section (`runtime/guest.s`) is the PS2's 32 MB of RAM at `0x00011000`:
  because it is part of the image, Windows places nothing else there (the stack, heaps and DLLs
  land above `0x02000000`, where the program's own sections start; `tools/game_link.py` writes the
  link script).
- The section is *shared*, so the uncached mirrors at `0x20000000` and `0x30000000` are second and
  third views of the same pages (`runtime/guest.cpp`): writes through any of them are visible
  through the others, as on the PS2. (A side effect: a second running gt4_game.exe would share the
  RAM; the runtime refuses to start twice.)
- The EE register blocks, VU memory, the GS privileged registers and the scratchpad are plain
  memory at their PS2 addresses; the functions that drive the hardware are replaced (below).
- At start the original executable's segments (`CORE.GT4.elf`, made from the user's disc by
  gt4-decomp's tools) are loaded at their addresses: the game code reads its data from there.

### Calls

- Every function of the original image gets a 5-byte trampoline at its address
  (`runtime/image.cpp`): `jmp` to its native code, or `call` into the missing-function handler.
  So function pointers in the original data (vtables, callbacks, constructor lists) work.
- The gcc 2.x C++ ABI is kept by compiling with gcc 2.9 itself: the vtable pointer after the
  fields, 8-byte `{delta, index, pfn}` vtable entries, destructors with the in-charge argument,
  gcc 2.x name mangling. Vtables and type info are never emitted by the native compile: they
  resolve to the originals in the loaded image.
- `tools/game_link.py` places every symbol the objects use but do not define at its PS2 address
  (`func_`/`D_` names, the decomp's symbol lists, mangled forms), so data references land in the
  loaded image and calls in trampolines. Unresolved names get addresses in the unmapped first
  64 KB: any use faults and the crash report names the symbol.
- Names the host C runtime also defines bind to the host's (`memcpy`...), except the ones that
  manage the game's memory or end it (`malloc`, `free`, `exit`...), which the game objects keep
  under `gt4g_` names.

### Services

| Area | File | What |
|---|---|---|
| EE kernel | `runtime/kernel.cpp` | threads (one runs at a time, EE priorities, preemption), semaphores, sleep/wakeup, INTC/DMAC handlers, alarms, 59.94 Hz VBlank; syscall stubs found by pattern |
| Interrupts | `runtime/kernel.cpp` | the running guest thread is frozen at any instruction while handlers run (as on the EE); held while it is in runtime code or has interrupts disabled |
| IOP | `runtime/iop.cpp` | SIF RPC replaced at its API; every server the game binds answered natively (docs/IOP_SERVICES.md): disc sectors from the ISO, file streams, the clock, SPU envelopes...; module loads succeed |
| SDK | `runtime/sdk.cpp`, `libc.cpp`, `setjmp.s`, `eeops.cpp` | DIntr/EIntr, libc, setjmp, the privileged instructions the compiler's asm translation calls |
| Graphics | `runtime/gs.cpp` | the two DMA-start functions walk the chains and stream VIF1/GIF data to the host; VBlank frame markers and GS registers |
| Link | `runtime/link.cpp` | the shared-memory rings to gt4_host, or a `--capture` file |

A runtime function named `gt4_replace_<SDK name or func_ADDR>` takes over that function: the link
points calls and the trampoline at it and leaves out the decomp object that defined it.

## Build and run

```
py tools/game_cc.py --decomp C:\path\to\gt4-decomp --out build/game --all   (compiler in WSL; objects in build/game/obj)
py tools/game_link.py --decomp C:\path\to\gt4-decomp --objs build/game/obj
build\game\gt4_game.exe --elf ...\orig\SCUS-97328\CORE.GT4.elf [--iso disc.iso] [--capture FILE]
build\game\gt4_game.exe --self-test        (kernel scheduling and interrupt checks)
```

`GT4_MISSING=continue` makes missing functions return 0 instead of stopping (to list what a run
needs; the results after the first one are not trustworthy). `tools/gt4dbg.c` is a minimal
debugger (exceptions with registers and the native functions on the stack; `--watch-write ADDR` /
`--watch-rw ADDR` report every write / access to a guest word with the writer and its callers;
`--scan WORD` or `--dump ADDR LEN` with `--scan-after SECONDS` read guest RAM during a run).
`GT4_WATCH=SECONDS` logs every guest thread's state and call chain; `GT4_DMA_LOG=N`
(`GT4_DMA_LOG_FROM=VBLANK`) logs the DMA tags walked, to find a packet in guest memory;
`GT4_TRACE_HITS=FILE` appends the PS2 address of each function compiled with
`GT4_CC_EXTRA=-finstrument-functions` the first time it runs: compile the provisional drafts that way
(`game_cc.py --list`) and compare two scenes to see which drafts only one of them reaches (that found
the texture upload chain 004A4F48 that broke GT mode's map).
Testing against the original code (docs/DIFFERENTIAL_TESTING.md): `GT4_PROFILE=FILE:FROM:TO` samples where a
scene spends its time; `GT4_RECORD` / `GT4_RECORD_ROOT` record calls of game functions for `tools/eeref` to
replay on the original MIPS code; `GT4_SNAPSHOT` writes the guest RAM at given VBlanks; with
`game_link.py --eeorig`, `GT4_ORIG=ADDR,...` or `GT4_ORIG=all` runs the original code in the game.
The game's x87 unit runs at single precision and rounds toward zero, as the EE's FPU does (`GT4_FPU_RC=nearest`
and `GT4_FPU_PC=d|e` for comparison).
`GT4_POKE=VBLANK:ADDR=WORD,...[;VBLANK:...]` writes guest words when a VBlank is reached (ADDR can be `*WORD±OFF`:
relative to the first guest word holding WORD, e.g. an object's vtable pointer), to put the game into a case under
test. `GT4_AI_CATCHUP=original|reduced|off` fixes the host's `gameplay.ai_catchup` option (the AI catch-up, GT4's
slow car boost: game_src/func_00365790.cpp).
`GT4_PAD_SCRIPT=VBLANK:BUTTON[:FRAMES],...` presses pad-1 buttons at given VBlanks when no host pad
is connected (e.g. `900:cross,1200:right,1320:cross` answers the boot's memory-card dialogs). The VBlanks are the
game's: once the menus run at 30 frames a second, a headless host presents about two frames per VBlank, so a
capture's frame number is not the VBlank (the host's closing `GS:` line gives both totals). On GT mode's map a
move takes effect after the map has scrolled (some 200 VBlanks); presses while it scrolls are lost.
`GT4_TRACE_PAD=1` logs each scripted press as the game reads it, `=all` every read of pad 1 with its VBlank.
`GT4_TRACE_INTC=CAUSE` logs, every 120th time, each INTC handler of that cause with its result (a handler returning
a negative value ends the chain). `GT4_WATCH` names the semaphore a waiting thread waits on, with its count.
`GT4_MONEY=N` (testing) keeps the player's money from reading below N (game_src/func_0043AF68.cpp), for the
dealers, tuning shops and purchases. `GT4_SAVES=DIR` (or the older `GT4_MEMCARD`) is the save folder (docs/SAVES.md), relative to the working directory.

Finding a wrong GS packet: decode the `--capture` file (GIF/VIF1 records), find the packet's guest
address with `GT4_DMA_LOG`, then its writer with `gt4dbg --watch-write`. Provisional drafts
(`build/game/draft_natives.txt`) are the first suspects: they are chosen by instruction distance,
and an `order` or `other` draft can behave differently (004AB318 stored each value one A+D slot
early; 004AA570 truncated its size to 16 bits).

Who writes what the GS receives: `GT4_GS_WRITERS=FROM,FRAMES` (runtime/gs_writers.cpp, debugging only) measures, from
VBlank FROM and over FRAMES frames, which game function wrote each 32-bit word the DMA channels delivered (DMA tags,
VIF codes, a 3D batch's vertices, GIF tag, constants and state packet, DIRECT and PATH3 GIF tags, A+D writes by
register group, REGLIST/PACKED vertices, IMAGE pixels), and who set the pgl shadow registers that the state flush
packed. It write-protects the guest pages that held packet data and logs each write's function through an exception
handler, so the game runs about ten times slower in the window. It writes `gs_writers_FROM.txt` (tab-separated
`site`, `regs`, `prims`, `kick`, `value`, `setter` lines; `GT4_GS_WRITERS_DIR` sets the folder,
`GT4_GS_WRITERS_IMAGE=1` also watches texture pixels). One window per run: a protected page of the shared RAM
section becomes private to the main view, so afterwards the mirrors at 0x20000000 / 0x30000000 read stale data and the
rest of the run is not reliable. The variable unset, nothing changes. The measurements of 2026-10-30 and what they
mean for a native renderer interface: docs/GRAPHICS_API_PLAN.md.

## Status

See PORT_TRACKER in gt4-decomp and the log below.

- 2026-10-25: 30,189 of 30,257 sources compile (99.8%); the full link places 27,000+ native
  functions; the game boots through its static constructors and IOP start-up into module loading.
