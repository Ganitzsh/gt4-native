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
- **Fixed first (d78a66d on main): the rings' 64-bit indices were not read or written atomically by the i686
  game.** A `volatile uint64_t` compiles to two 32-bit accesses (checked with the game's compiler, MinGW gcc 16.2 i686
  at -O2: `movl` low word, then `movl` high word), and the low word wraps every 4 GiB of traffic, which a long session
  crosses several times (menus_boot.cap's session was 14.5 GB). Two cases:
  - the game's own head store: under x86 ordering (low word, then high) a torn read by the host can only look smaller,
    and `head <= tail` makes the host retry; with FEX's TSO emulation off (an option some setups turn off for speed)
    the host may see the new high word first, a `head` about 4 GiB ahead, and read garbage as records;
  - the game's read of the host's `tail` (and `head` of the other ring): two 32-bit loads that straddle the host's
    8-byte update can combine the new high word with the old low word, a `tail` 4 GiB ahead, **even under x86
    ordering**, and the game then overwrites records the host has not read. This one was a risk on Windows as well.
  The fix, in `runtime/link.cpp` (lane C's, lane A reviewing every change; PLAN.md): every access to a ring's head or
  tail is one 8-byte `__atomic_load_n` / `__atomic_store_n` (acquire / release), one SSE `movq` on i686; the commit
  message lists the disassembly of all seven accesses before and after. FEX should keep an aligned `movq` single-copy
  atomic; the probe below checks it.

## Checks before the device

- **Here, on Windows:** the file-backed link end to end (the host creates a file, the game opens it by path), with
  race7 and a live session identical to the named-section link; the 64-bit fix (done: d78a66d, a live race and ORIG
  race7 unchanged).
- **Host build:** the POSIX `GameLink` (`start`, `poll`, `write`, exit code) in `link_host.cpp` builds for x86_64
  and aarch64 Linux with zig 0.17.1 (`build/tools`, SHA-256 checked), glibc 2.35 until Armada's version is known.
- **On the Odin (the coordinator runs it):** first a two-process probe: a native aarch64 writer and an i686 Windows
  reader under Wine/FEX on one `/dev/shm` file, counting a million ordered records with TSO emulation on and off,
  with the ring's indices started just below 2^32 so the low word wraps during the run (the case the 8-byte fix is
  about; a million records alone never wraps it).
  Then the aarch64 host `--replay` on race7 (ORIG against x64), then the live game.

## On the Odin (2026-11-08, run by the coordinator)

- **Device:** Armada Linux, aarch64, glibc 2.43 (so building for 2.35 is fine). There is no plain `wine`: x86 Windows
  programs run through Proton's ARM64 build, `<proton>/files/bin-arm64/wine` (Steam's Proton 11.0 (ARM64), or
  proton-cachyos 11.0 arm64 in `/usr/share/steam/compatibilitytools.d`), with that `bin-arm64` folder on `PATH` and
  `WINESERVER=<proton>/files/bin-arm64/wineserver`, or it stops with "could not exec wineserver". So the host's
  `GT4_GAME_LAUNCHER` is that `wine`, in that environment.
- **The probe passed both ways** (the first version, indices stepping by 64): with FEX's default TSO emulation and
  with `FEX_TSOENABLED=0` (whether that variable took effect is unconfirmed, but the host side ran 6 times faster),
  1,000,000 of 1,000,000 records intact each way, 0 bad, the low word wrapped. So a `/dev/shm` file maps shared between
  the native host and the game under Proton's Wine on FEX, and the link's 8-byte indices and barriers hold. That probe
  could not have shown a torn index (one crossing a run); the probe now steps its indices by 2^32 + 64 so every update
  changes both words, and its control build with split 32-bit accesses fails even on x86.
- **The probe that can fail, run at 03:16** (indices stepping by 2^32 + 64; the coordinator checked the binaries'
  hashes against the ones served): with `FEX_TSOENABLED=1` and `=0`, the fixed build PASSED both times, 1,000,000 of
  1,000,000 records intact each way, 0 bad, 0 impossible indices (host side 1.7-1.8 s, game side 176-193 ms). The
  control (`-DPROBE_TORN`, the game's index accesses before the 8-byte fix) FAILED both times, as it should: with
  TSO on 1,643 impossible indices on the game side and 1,962 on the host side, with TSO off 1,723 and 5,918 (the
  records themselves stayed intact: a torn index is caught before its data is read). So the probe sees the failure
  on the device, and the link's fixed path is clean across the native host and the i686 game under Proton on FEX.
- **Shutdown:** a `SIGTERM` to the launcher ends it (it becomes `start.exe`), but the Windows processes it started keep
  running. The host now runs `wineserver -k` (the same `WINESERVER` and `WINEPREFIX`) after the `SIGTERM`, then
  `SIGKILL`s the launcher if needed.
