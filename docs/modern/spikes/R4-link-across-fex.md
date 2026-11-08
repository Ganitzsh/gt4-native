# R4: the host and the game across the FEX boundary (design note)

The Odin 3 runs a native **aarch64 Linux** `gt4_host`; the game stays the **i686 Windows** `gt4_game.exe`, run by
Wine on FEX (x86 emulation). This note says what the two share today, what does not cross that boundary, and how the
link will cross it, before the aarch64 host is built further (the coordinator's ask, 2026-11-08).

## What the link is today (Windows)

- **Memory:** the host creates a 128 MiB named section, `Local\gt4_link_<host pid>` (`link_host.cpp`,
  `CreateFileMappingA`); the game opens it by name (`runtime/link.cpp`, `OpenFileMappingA` + `MapViewOfFile`). Two
  rings (game to host 96 MiB, host to game 8 MiB) and a header (`include/gt4/link/protocol.h`).
- **Waiting:** none of Windows' objects. Both sides poll: the game `Sleep(1)` while a ring is full, the host
  `sleep_for(1 ms)`. No events, semaphores or mutexes cross the processes.
- **Ordering:** the producer writes the record, then a full barrier, then the ring's `head`; the consumer reads
  `head`, then a barrier, then the data, and releases with `tail`. The game uses `MemoryBarrier()`, the host
  `std::atomic_thread_fence` acquire / release. `head` and `tail` are `volatile uint64_t`.
- **Process:** the host starts the game with `CreateProcess(... --link <name>)` and waits on its handle for the exit
  code. The game writes its own CPU time into the header (`game_cpu_ms`), already because Wine's `GetProcessTimes` on
  another process gives the caller's.
- **The guest memory** (`runtime/guest.cpp`) is the game's own in-process section of its executable image; the host
  never maps it. Its per-executable mutex is a named object inside Wine, which is fine.

## What does not cross

- A **named section** lives in wineserver: a native Linux process cannot open `Local\gt4_link_...`.
- **`CreateProcess`** does not exist for the native host.

Everything else is plain memory plus polling, which crosses as it is, if the memory is truly shared.

## The plan (PLAN.md C9, the POSIX link)

1. **Memory: a file in `/dev/shm`.** The host (POSIX build of `link_host.cpp`) creates `/dev/shm/gt4_link_<pid>`
   (`shm_open`, `ftruncate` 128 MiB, `mmap MAP_SHARED`) and fills the header as today. The game opens a path instead
   of a name: `--link Z:\dev\shm\gt4_link_<pid>` (Wine's `Z:` drive is `/`), `CreateFileA` + `CreateFileMappingA` on
   that file + `MapViewOfFile`. Wine is expected to map a file-backed section as a shared Unix mapping of the file,
   so both processes would see the same pages (to be confirmed by the probe on the device, and by a Linux x86_64
   Wine run where one is available). On Windows `link_open` keeps the named section; the file path is chosen when
   `--link` names a file. The same path works on Windows with an ordinary file, so it can be tested here before the
   Odin.
2. **Launch:** `posix_spawn` of a configured launcher, `GT4_GAME_LAUNCHER` (default `wine`), with the game's
   arguments: `wine gt4_game.exe --link Z:\dev\shm\gt4_link_<pid> --elf ...`. Armada runs x86 Wine through FEX
   (binfmt), so `wine` is enough there; an explicit `FEXInterpreter /path/to/wine` also works. The host waits with
   `waitpid` for the exit code. The header's `game_pid` is then Wine's, informational only.
3. **Waiting:** unchanged (1 ms polling on both sides).

## Memory ordering under FEX

- The game's ordering is x86's: TSO, plus `MemoryBarrier()` (a locked instruction) before each `head` store and after
  each `head` load. FEX is expected to translate a locked instruction or `mfence` to a full barrier (`DMB ISH`)
  whether or not its TSO emulation is on, which keeps the publish and consume barriers under FEX (to be checked on the
  device, by the probe below). The host's acquire / release fences are native aarch64. If that holds, the ring
  protocol is correct across the boundary as written.
- **One fix needed first: the 64-bit index is not stored atomically by the i686 game.** `g_hdr->g2h.head = head +
  total` on a `volatile uint64_t` compiles to two 32-bit stores (checked with the game's compiler, MinGW gcc 16.2
  i686 at -O2: `movl` low word, then `movl` high word). Under x86 ordering (low word, then high), a torn read
  by the host is harmless: the value can only look smaller, and `head <= tail` makes the host retry. But with FEX's TSO
  emulation off (an option some setups turn off for speed), the host may see the new high word before the new low
  word, a `head` about 4 GiB ahead, and read garbage as records. The low word wraps every 4 GiB of traffic, which a
  long session crosses several times (menus_boot.cap's session was 14.5 GB). The fix is a single 64-bit store and load
  in the game (`__atomic_store_n(..., __ATOMIC_RELEASE)` and `__atomic_load_n`: one 8-byte SSE `movq` on i686,
  checked with the same compiler, which FEX should keep single-copy atomic; the probe checks it). It is right on Windows too, where the same tear can in principle
  happen. `runtime/link.cpp` is not in PLAN.md's ownership table: the coordinator should say whose it is.

## Checks before the device

- **Here, on Windows:** the file-backed link end to end (the host creates a file, the game opens it by path), with
  race7 and a live session identical to the named-section link; the 64-bit store change with ctest and a long
  session past a 4 GiB wrap of the game-to-host ring.
- **Host build:** the POSIX `GameLink` (`start`, `poll`, `write`, exit code) in `link_host.cpp` builds for x86_64
  and aarch64 Linux with zig 0.17.1 (`build/tools`, SHA-256 checked), glibc 2.35 until Armada's version is known.
- **On the Odin (the coordinator runs it):** first a two-process probe: a native aarch64 writer and an i686 Windows
  reader under Wine/FEX on one `/dev/shm` file, counting a million ordered records with TSO emulation on and off.
  Then the aarch64 host `--replay` on race7 (ORIG against x64), then the live game.
