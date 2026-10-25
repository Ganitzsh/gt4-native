# IOP and kernel services the game needs

What Gran Turismo 4 (USA, SCUS-97328, v1.01) asks of the IOP and of the EE kernel, written for the
native port: every item here is a host service to implement. Sources: the matched EE code in
gt4-decomp (`src/`, `config/symbol_addrs.txt`, `config/sdk_functions.tsv`), the original
`CORE.GT4.elf`, the IOP modules on the disc (`IRX/*.IRX`, `IOPRP300.IMG`) and the Sony SDK 3.0.2
libraries the game linked (names and syscall numbers only).

Conventions:

- Addresses are EE virtual addresses in `CORE.GT4.elf` unless stated (IRX offsets are `.text`
  offsets inside the module). `func_XXXXXXXX` names are the decomp's; a path under `src/` means
  the function is matched.
- **[V]** verified in the code (disassembly or matched source, address given); **[I]** inferred
  (from names, structure, SDK knowledge or one unconfirmed path). Unmarked statements in a section
  marked [V] are verified.
- "Server ID" is the 32-bit SIF RPC server number; the game's own servers use four ASCII
  characters, written here as the hex value and the characters in big-endian order
  (`0x50434456` = `PCDV`).

## 0. Summary for the host

The game boots its IOP itself: it reboots the IOP with `cdrom0:\IOPRP300.IMG;1`, then loads 23
modules from `cdrom0:\IRX\` (22 when no network configuration is found). Every IOP service it then uses goes through one of two doors:

1. **Sony SDK client libraries linked in the EE image** (libcdvd, filestub/fileio, libmc,
   libpad2 over libdbc, iopheap, eeloadfile, msifrpc, libeyetoy): the host can replace these at
   the SDK function boundary (HLE `sceCdRead`, `sceMcOpen`, `scePad2Read`...) instead of
   emulating their RPC traffic. Their RPC protocols are the standard Sony ones.
2. **Polyphony's own IOP modules** (`PDICDVD`, `PDISTR`, `PDISPU2`, `PDIUSB`, `LIBPDI`, `LGDEV`),
   reached from game code (`src/libstream/`) through `sceSifCallRpc` on four-character server
   IDs. These protocols are game-specific and are the core of this document (sections 5-6).

| Host service | What the game uses | Section |
|---|---|---|
| Kernel: threads, semaphores, interrupts, alarms, caches | about 50 syscalls, 10 game threads started at boot, 5 VBlank (INTC 2) handlers at boot | 2, 3, 4 |
| Frame timing | VBlank-start (INTC 2) counter in scratchpad, threads sleeping on it | 4 |
| IOP boot | `sceSifRebootIop`, `sceSifLoadModule` x23 (must succeed; the loaders retry forever) | 1 |
| Disc | `PCDV` raw reads by absolute LBA (ISO9660 + VOL TOC), layer-1 start; `Pcdv` 100 ms status push (RTC seconds, drive state) | 5.1, 6 |
| Files / streams | `STRP` open-by-LBA + sequential read into EE memory, file -> SPU RAM upload | 5.2, 6 |
| Music | `PBGM`: the IOP streams `.ads` from an LBA itself; status polled each VBlank | 5.3 |
| Voice | `VOIC`: the EE decodes Speex and pushes 48 kHz PCM blocks | 5.4 |
| Movies | `MPG1`/`MPG2`: the IOP reads and demuxes PSS, plays the audio, hands video PES to the EE (IPU) | 5.5 |
| Sound effects / sequencer | `SPUP`: a 2 x 0x1D0-byte SPU2 register image every VBlank, returns ENVX | 5.6 |
| Pad | libpad2 (`scePad2Read`, 18-byte button data) + libvib, 2 ports, read each VBlank | 7 |
| Memory card | libmc via a worker thread; directory `BASCUS-97328GAMEDATA` etc. | 8 |
| Logitech wheel, USB storage/keyboard/mouse, EyeToy, printer, network | optional peripherals; can be stubbed (binds must still succeed) | 5.7-5.10 |

**Every server the game binds must exist**: the bind wrappers (`00577FC8`, `0058BCE8`, the SDK
`sceCdInit`/`sceMcInit`/`sceDbcInit`) loop until `sceSifBindRpc` returns a server. A host that
does not implement a peripheral must still accept the bind and answer the init calls.

## 1. Boot sequence (EE side)

### 1.1 Before `main` [V]

`SCUS_973.28` (the `BOOT2` ELF of `SYSTEM.CNF`) runs first [V: strings and their references in
the loader]: it reboots the IOP with `cdrom0:\IOPRP300.IMG;1` (through `rom0:UDNL`), loads
`cdrom0:\IRX\` `SIO2MAN`, `MTAPMAN`, `MCMAN`, `MCSERV`, `PADMAN`, then loads `core.gt4` (image
loader classes for DISK, MCARD 0/1 and HOST; it also names `.in.notice2005.img`) and runs it. A
native port starts directly at `CORE.GT4.elf`'s entry; CORE reboots the IOP again anyway, so
nothing the loader set up survives. In `CORE.GT4.elf`:

1. crt0 `00100008`: clears `.bss` (`006D5E00`-`008A215C`), `SetupThread` (syscall `0x3C`; gp
   `006DDDF0`, stack top `01FF8000`, stack size `0x8000`, argument block `006D6380`, root
   `00100228` = `ExitThread`), `SetupHeap` (`0x3D`; heap from `008A215C`, size -1).
2. `_InitSys` `005B7560`: `supplement_crt0`, `InitSystemCallTableAddress`, `InitAlarm` (installs the
   alarm syscalls `0xFC`/`0xFD`/`-0xFE`/`-0xFF` through `SetSyscall` `0x74`), `InitTimer` (EE
   timer 2, `AddIntcHandler2(11 = TIMER2, cbTimerHandler 005B8158)`), `StartTimerSystemTime`,
   `InitThread` (creates the SDK idle thread `topThread 005AE9A0`, a semaphore, and changes the
   main thread's priority), `InitExecPS2`, `InitTLBFunctions`. These patch the kernel through
   syscalls `0x5A` (kernel copy), `0x5B` (get entry address), `0x74`, `0x83`: on the host they are
   no-ops, but the alarm and timer services they install must exist (section 2.2).
3. `FlushCache(0)`, `0048EF90`, then `main(argc, argv)` = `00107F08`.

### 1.2 `main` `00107F08` [V]

```
00107F08 main
  005BC588  global constructors (once); among them 00101E28 builds the IOP module tables (1.3)
  00107E88  init
    00578928  ChangeThreadPriority(GetThreadId(), 64)          main thread -> priority 64
    00102308  IOP boot: reboot + module tables (1.3)            <- most IOP work happens here
    004AE1B8  -> 004ACC68 file-system init: file-device threads (004B0708), then mounts the
              two VOL devices (/gt4.vol, /gt4l1.vol) synchronously over PCDV (section 6)
    00108BF8  game init: malloc of a timer-dependent size (GetTimerSystemTime & 0x3F),
              databases, scripts, BGM play list...
  00108040  game-mode state machine on the main thread (never returns)
  00107ED8  teardown (only reached on exit): 00101DD8 unloads/ends services
```

The frame loop does not run on the main thread: it runs on the **game thread** `001016E8`,
created during the IOP boot by a module-table callback (1.3, step A2), at priority 24 (higher than
main's 64). From then on the main thread only runs while the game thread sleeps (VBlank waits,
semaphores), so module loading continues in the background of the first frames.

### 1.3 IOP boot `00102308` [V]

`00102308` fills a list at `006DE7E0` of four module tables (`{table, path prefix, suffix}`), builds
an initializer object (vtable `00689E00`, constructor `00577C98`) with the reboot image
`cdrom0:\IOPRP300.IMG;1`, and runs its two virtual steps (`00577948`):

**Step 1, `00577D48`** (reboot):

1. `sceSifInitRpc(0)` (`005779E8`), `sceCdInit(0)` (`SCECdINIT`), `sceCdMmode(2)` (`SCECdDVD`).
2. `00577DD0` -> `00577A30`: `while (!sceSifRebootIop("cdrom0:\IOPRP300.IMG;1")) DelayThread(2000);
   while (!sceSifSyncIop()) DelayThread(2000);`
3. Again `sceSifInitRpc(0)`, `sceCdInit(0)`, `sceCdMmode(2)`.
4. `00577A08`: `sceSifLoadFileReset()`, `sceFsReset()` (rebinds fileio `0x80000001`).

**Step 2, `005779C8` -> `00577A90`**: for each table, each 0x28-byte entry is
`{predicate, before, after (three {fn, arg} pairs), name, args, argsize, active}`
(`00577340`, `005772E8`, `00577240`, `00577290`, `00577198` build them; defaults `00577168`
returns 1, `00577180` does nothing). `005773B8` runs an entry: if `predicate()`: `before()`, then
if `name`: load `prefix + name + suffix`, then `after()`. The load (`00577518`) upper-cases the
path and turns `/` into `\` for `cdrom...:` paths ending in `;1` (`00577AF0`), then
`while (sceSifLoadModule(path, argsize, args) < 0) DelayThread(2000);` -- **a module that fails
to load hangs the boot**, so the host must accept every load.

The tables (built at static-init time by `00101E28`; prefix `cdrom0:\IRX\`, suffix `;1`):

| # | Table A (`00617D98`) | args | callback after the step |
|---|---|---|---|
| A1 | (callback only) `00101A70` -> `005524B8` | | computes the local-time offset (libscf time zone); no IOP |
| A2 | (callback only) `00101858` | | **creates and starts the game thread** (`001016E8`, stack `006D67E0` size `0x8000`, priority 24) and the game object `GameObjectPS2::start` |
| A3 | `SIO2MAN.IRX` | | |
| A4 | `MCMAN.IRX` | | |
| A5 | `MCSERV.IRX` | | |
| A6 | `SIO2D.IRX` | | |
| A7 | `DBCMAN.IRX` | | |
| A8 | `DS2U_D.IRX` (`PAD2\DS2U_D.IRX` when `D_006182A0` = 0; it is 1) | | |
| A9 | (callback only) `00101970` | | `while (sceDbcInit() != 1) DelayThread`; pad init `00551810` (section 7); memory-card manager `0054C430` (`sceMcInit` until 0, worker threads; section 8) |
| A10 | `LIBSD.IRX` | | |
| A11 | `USBD.IRX` | | |
| A12 | `INET.IRX` | | |
| A13 | `NETCNF.IRX` | `icon=cdrom0:\NET\SYS_NET.ICO;1` | |
| A14 | `INETCTL.IRX` | `-no_decode` | |
| A15 | `DEV9.IRX` | | |
| A16 | `SMAP.IRX`, only if predicate `00102788` (network config present: `004ED008`, `D_00645430`) | | |
| A17 | `MSIFRPC.IRX` | | |

| # | Table B (`00618068`) | callback after loading |
|---|---|---|
| B1 | `LIBPDI.IRX` | |
| B2 | `PDICDVD.IRX` | `00101930` -> `00548500`: binds `PCDV` and `Pcdv`, first call (section 6) |
| B3 | `PDISPU2.IRX` | |
| B4 | `PDISTR.IRX` | `00101A10`: binds `STRP` (`00550D00`), `PBGM` + `VOIC` (`00547D88`), `MPG1`/`MPG2` (`0054F340`), sound system `00559DB0` (binds `SPUP`/`SPUT` `0055E5C8`, starts the sound VBlank thread `0055EB50`) |
| B5 | `PDIUSB.IRX` | `001026D0`: USB object `00553698` (EE-side RPC server `BSUP` `0x42535550`, threads `00553B80`/`00553C08`), `00552690`, `005532C0` |
| B6 | `USTORAGE.IRX` | `00102738` -> `00554350`: USB storage client (`PUST` `0x50555354`) |
| B7 | `LGDEV.IRX` | `00101AF0` -> `00555180`: Logitech wheel thread (`0x046D046D`) |
| B8 | `POWOFF.IRX` | |

| # | Table C (`006181D0`) | |
|---|---|---|
| C1 | `RTINETB.IRX` | args `-send_delay=3` |
| C2 | (callback) `001027B0` | `sceSifMInitRpc(0)` (msifrpc) |

Table D (`00618248`) holds one empty callback `00101B18`.

So the full IOP module order is: IOPRP300.IMG (reboot), SIO2MAN, MCMAN, MCSERV, SIO2D, DBCMAN,
DS2U_D, LIBSD, USBD, INET, NETCNF, INETCTL, DEV9, [SMAP], MSIFRPC, LIBPDI, PDICDVD, PDISPU2,
PDISTR, PDIUSB, USTORAGE, LGDEV, POWOFF, RTINETB. Later, on demand: `EYETOY.IRX`
(`001C56B0`, `sceSifLoadModule`), `pdinetcnf.irx` (`004ED1F8`, after
`sceSifSearchModuleByName("PDI_NETCNF")`), network modules sent from EE memory
(`_sceSifLoadModuleBuffer` from `00520758`, after `sceSifAllocSysMemory` + `sceSifSetDma`).

Other boot-time SDK calls: `sceGsResetGraph(0, 1, 2, 1)` (interlace, NTSC, frame mode) from the
game thread's first step (`00107730` -> `004A0398` -> `004A03C0`), display 640x448 (section 4).
`sceErxInit` (EE-side relocatable modules) is not called at boot: only by the photo-printing path
(`00101C50`, Epson ERX drivers in `EPSON/`).

### 1.4 Threads [V]

The game creates threads through `00578968(entry {fn, arg}, stack {ptr, size}, prio)` (stack
allocated, 16-aligned, default size `0x2000`; `CreateThread` with entry trampoline `005786F0`,
gp `006DDDF0`) and starts them with `005788B8` (`StartThread`). Priorities come from
`00578B50(class, delta)`: class 0 -> 1, 3 -> 13+d (2..23), 5 -> 14+d, 4 -> 24+d (24..31),
1 -> 64+d (32..95), 2 -> 112+d (96..126). The trampoline calls `fn(arg)`, then
`ExitDeleteThread`.

| Entry | Created by | Priority | Stack | Role |
|---|---|---|---|---|
| (main) | crt0 | 64 (`00578928`) | `0x8000` at `01FF8000` | game-mode state machine `00108040` |
| `001016E8` | `00101858` (boot A2) | 24 | `0x8000` at `006D67E0` | **game thread**: per-frame loop (section 4) |
| `00551758` | `00551810` (boot A9) | 13 | default | pad reader, wakes on VBlank (section 7) |
| `00549D98` | `0054C430` (boot A9) | 13 | `0x1000` at `0086E600` | memory-card worker (13 commands) |
| `0054A230` | `0054C430` (boot A9) | 13 | default | memory-card poll, wakes on VBlank |
| `005477C8` -> `00547550` | `005477E8` (boot B4, BGM) | 13 | `0x2000` | BGM command worker: sends the `PBGM` RPCs |
| `0055EB50` | `0055EC60` (boot B4, sound) | 14 | default | sound update, wakes on VBlank |
| `00553B80` | `005537C8` (boot B5) | 12 | `0x1000` at `008700C0` | EE-side RPC server (`sceSifRegisterRpc` + `sceSifRpcLoop`) |
| `00553C08` | `005537C8` (boot B5) | 14 | `0x1000` at `008710C0` | USB event processing |
| `005552B8` (via `00555208`) | `00555180` (boot B7) | 13 | in object | Logitech wheel, wakes on VBlank |
| `00558578`, `005585A0` | `005585C8` (Speex narration player) | 15, 112 | default | Speex decode + `VOIC` push; volume |
| `0054DB78`, `0054DB98`, `0054E948` | `mpegif` ctor `0054DBB8`, `0054E3D8` | 113, 13, 13 | | movie (IPU) decoding, `MPG1` read thread |
| `00123568` | `mLoggerControl` ctor | 65 | | race data logger |
| `00213B68` | `mUpdateContextPS2` | 65 | | script update context |
| `004B0210`, `004B0230` | `004B0708` (from `004ACC68`, file-system init) | 65, 12 | | file-device threads (PDISTD) |
| `00502490`, `00503780` | network | 13, 25 | | network |
| `0057FFB0` thread | libcdvd | | | SDK CD callback thread |

Semaphores: `CreateSema` is called directly by 8 game functions (`004A03C0` render lock, `004A0D00`
DMA channel semaphores, `00539A78`, `0054DBB8`, `0054FDD0`, `00560778`, `00564610`, and the
generic `005782E8(init, max)` used for every VBlank-signalled thread) and by the SDK libraries.
`00578480` signals from interrupt context (`iSignalSema`), `00578500` waits (`WaitSema`).

## 2. EE kernel syscalls

### 2.1 The syscall stubs [V]

The SDK `libkernl.a` member `klib.o` is linked whole at `005AD880`-`005AE11F` (one 16-byte stub
`li $v1, N; syscall; jr $ra; nop` per call, in syscall-number order); names come from the SDK
3.0.2 `libkernl.a` symbol table (matched by offset: `AddIntcHandler` at `klib.o+0x100` =
`005AD980`). Call counts are direct `jal`/`j` sites; "game" counts callers outside the SDK
(outside `config/sdk_functions.tsv` and `config/compiler_sdk.txt`). Stubs with no caller are left
out (list at the end).

| No. | Stub | Name | Sites | Game callers | Called by (SDK) |
|---|---|---|---|---|---|
| 0x02 | 005AD8A0 | SetGsCrt | 2 | 0 | sceGsResetGraph |
| 0x04 | 005AD8C0 | Exit | 1 | 0 | (exit path) |
| 0x06 | 005AD8E0 | LoadExecPS2 | 1 | 0 | LoadExecPS2 |
| 0x07 | 005AD8F0 | ExecPS2 | 1 | 0 | ExecPS2 |
| 0x0D | 005AD950 | SetVTLBRefillHandler | 4 | 0 | SetTLBHandler, SetDebugHandler |
| 0x0E | 005AD960 | SetVCommonHandler | 1 | 0 | SetDebugHandler |
| 0x10 | 005AD980 | AddIntcHandler | 17 | 5 | |
| 0x10 | 005AD990 | AddIntcHandler2 | 7 | 5 | InitTimer, DummyEloadpReserve |
| 0x11 | 005AD9A0 | RemoveIntcHandler | 15 | 10 | EndTimer, sceGsResetGraph |
| 0x12 | 005AD9B0 | AddDmacHandler | 4 | 1 | sceSifInitCmd |
| 0x12 | 005AD9C0 | AddDmacHandler2 | 2 | 2 | |
| 0x13 | 005AD9D0 | RemoveDmacHandler | 6 | 3 | sceSifExitCmd |
| 0x14-0x17 | 005AD9E0-A10 | _EnableIntc, _DisableIntc, _EnableDmac, _DisableDmac | 1 each | | SDK wrappers `EnableIntc` `005AE7E8`, `DisableIntc` `005AE780`, `EnableDmac` `005AE8B8`... |
| -0x1A..-0x1D | 005ADA40-A70 | _iEnableIntc, _iDisableIntc, _iEnableDmac, _iDisableDmac | 1 each | | wrappers `005AE920`-`005AE980` |
| 0x20 | 005ADAA0 | CreateThread | 6 | 2 | InitThread, liberx; game: `00578968`, `0054F7E0` |
| 0x21 | 005ADAB0 | DeleteThread | 4 | 1 | |
| 0x22 | 005ADAC0 | StartThread | 1 | 0 | via `StartThread` `005AEDE0` (3 game callers) |
| 0x24 | 005ADAE0 | ExitDeleteThread | 2 | 1 | thread trampoline `005786F0` |
| 0x25 | 005ADAF0 | TerminateThread | 3 | 1 | |
| 0x29 | 005ADB30 | ChangeThreadPriority | 24 | 10 | |
| 0x2B | 005ADB50 | RotateThreadReadyQueue | 1 | 0 | topThread |
| 0x2F | 005ADB90 | GetThreadId | 32 | 25 | |
| 0x30 | 005ADBA0 | ReferThreadStatus | 4 | 3 | |
| 0x32 | 005ADBC0 | SleepThread | 14 | 12 | sceSifRpcLoop |
| 0x33 | 005ADBD0 | WakeupThread | 10 | 7 | |
| -0x34 | 005ADBE0 | iWakeupThread (kernel) | 1 | 0 | via `iWakeupThread` `005AEB68` (9 game sites) |
| 0x35 | 005ADBF0 | CancelWakeupThread | 3 | 3 | |
| 0x37 | 005ADC10 | SuspendThread | 1 | 0 | topThread |
| -0x38 | 005ADC20 | iSuspendThread | 1 | 0 | |
| 0x3E | 005ADC80 | EndOfHeap | 1 | 0 | sbrk |
| 0x40 | 005ADCA0 | CreateSema | 57 | 8 | SDK libraries |
| 0x41 | 005ADCB0 | DeleteSema | 116 | 8 | |
| 0x42 | 005ADCC0 | SignalSema | 158 | 30 | |
| -0x43 | 005ADCD0 | iSignalSema | 17 | 2 | `00578480` (game), SIF/CD/fs interrupt callbacks |
| 0x44 | 005ADCE0 | WaitSema | 101 | 29 | |
| 0x45 | 005ADCF0 | PollSema | 26 | 1 | |
| 0x4A | 005ADD40 | SetOsdConfigParam | 2 | 0 | |
| 0x4B | 005ADD50 | GetOsdConfigParam | 10 | 0 | `sceScfGetLanguage`, `sceScfGetAspect`, time zone... |
| 0x64 | 005ADF20 | FlushCache | 56 | 28 | |
| 0x6B | 005ADF70 | sceSifStopDma | 1 | 0 | sceSifResetIop |
| 0x6F | 005ADFB0 | GetOsdConfigParam2 | 3 | 0 | libscf |
| 0x71 | 005ADFE0 | GsPutIMR | 1 | 0 | sceGsResetGraph |
| 0x73 | 005AE010 | SetVSyncFlag | 1 | 0 | VSync2 (unused by the game) |
| 0x76 | 005AE040 | sceSifDmaStat | 7 | 5 | network module upload, msifrpc |
| 0x77 | 005AE060 | sceSifSetDma | 10 | 5 | network module upload, msifrpc, sifcmd |
| -0x77 | 005AE070 | isceSifSetDma | 1 | 0 | _sceSifSendCmd |
| 0x78 / -0x78 | 005AE080 / 005AE090 | sceSifSetDChain / isceSifSetDChain | 1 / 1 | 0 | sifcmd |
| 0x79 | 005AE0A0 | sceSifSetReg | 8 | 0 | sifcmd, sifrpc, iopreset |
| 0x7A | 005AE0B0 | sceSifGetReg | 7 | 0 | sifcmd, sifrpc, iopreset |
| 0x7B | 005AE0C0 | ExecOSD | 1 | 0 | |
| 0x7C | 005AE0D0 | Deci2Call | 12 | 0 | deci2 (debug) |
| 0x7E | 005AE0F0 | MachineType | 1 | 0 | sceErxInit |
| 0x7F | 005AE100 | GetMemorySize | 1 | 0 | InitTLB |
| 0x82 | 005AE110 | _InitTLB | 1 | 0 | InitTLB |
| 0xFC | 005ADA20 | SetAlarm (installed by InitAlarm) | 1 | 1 | `0054C8F0` (movie) |
| 0xFD | 005ADA30 | ReleaseAlarm | 1 | 1 | `0054C8F0` |

Outside `klib.o` [V]: crt0 `001001C4` (`0x3C` SetupThread) and `001001E0` (`0x3D` SetupHeap);
`00100228` (`0x23` ExitThread, the main thread's return); the kernel patchers in `exit.o`,
`alarm.o`, `initsys.o`, `libosd.o`, `tlbfunc.o` (`005B7380`-`005B79E8`, `005B98C0`-`005B9AA0`:
`0x5A` copy into kernel memory, `0x5B` get entry address, `0x74` SetSyscall, `0x83` find address,
`0x55`-`0x59` TLB entry calls); `thread.o` `005AEB74` / `005AEC8C` (`-0x2F` inside
`iWakeupThread`/`iSuspendThread`). None of these is reached by game code at run time except via
`_InitSys`.

Never called directly: ResetEE, AddSbusIntcHandler/RemoveSbusIntcHandler, Interrupt2Iop,
SetVInterruptHandler, ExitThread (except crt0's root), iTerminateThread, Disable/EnableDispatch,
iChangeThreadPriority, ReleaseWaitThread, ResumeThread, JoinThread, ReferSemaStatus, the event
flags (0x50-0x5A in this SDK are reserved names), Enable/DisableIntcHandler/DmacHandler, KSeg0,
EnableCache/DisableCache, CpuConfig, SetCPUTimer*, GsGetIMR, SetPgifHandler, _print, PSMode,
iSetAlarm, iReleaseAlarm.

### 2.2 SDK wrappers that matter [V]

| Wrapper | Address | Game call sites | What it needs from the host |
|---|---|---|---|
| `DIntr` | `005B72A8` | 109 sites in 93 functions | disable interrupts (`di` + loop until Status.EIE clear), returns the old state; the caller re-enables with an inline `ei` (pattern `if (DIntr()) ei;`) or `EIntr` `005B72F8`. Host: a global interrupt lock around guest critical sections; must exclude the host's VBlank/DMA "interrupt" callbacks |
| `iWakeupThread` | `005AEB68` | 9 sites (VBlank handler `004AB430`...) | wake a sleeping thread from interrupt context |
| `DelayThread` | `005AED18` | 3 game (`00577F80` "yield 2 ms" used by every retry loop, `00578B10`, `0053AE38`) + CD/fs SDK | sleep N microseconds (built on `SetAlarm`) |
| `GetTimerSystemTime` | `005B8450` | 3 (`00579358` seed/clock, `00578560` microseconds, `0053B340`) | 64-bit bus-clock time (147.456 MHz) from EE timer 2 |
| `iSetTimerAlarm` | `005B9068` | network only (`00503688`), SIF RPC timeouts | timer-2 alarm |
| `EnableIntc` / `DisableIntc` / `EnableDmac` | `005AE7E8` / `005AE780` / `005AE8B8` | renderer `004A03C0`, `004A0D98`, `004A0738` | |
| `FlushCache(0)` / `_sceSDC` / `_sceIDC` | `005ADF20`, `005AE4F0`, `005AE630` | 28 game functions, `00576AD8` before every RPC send | no-op on the host (no data cache), but keep ordering |
| `scePrintf`, `write`, `open`, `read` | kprintf2.o, glue.o | a few | debug output, `host0:` (not used in retail paths [I]) |
| `LoadExecPS2`, `ExecOSD` | exit.o | 1 each | quit to the browser / reboot (exit paths only) |

### 2.3 Hardware the game also touches through COP0 [V]

The renderer reads and resets the performance counters (COP0 register 25: PCCR, PCR0, PCR1) in its
VBlank handler and init (`004A03C0`, `004AB430`) to accumulate per-frame cycle counts at
scratchpad `0x700020A0`/`0x700020A8`. The host should return monotonic values (or 0).

## 3. Interrupts

### 3.1 Handlers registered by the game [V]

INTC causes: 0 GS, 2 VBlank start, 3 VBlank end, 5 VIF1, 8 IPU, 11 timer 2. DMAC channels: 0
VIF0, 1 VIF1, 2 GIF, 3 fromIPU, 5 SIF0.

| Registered in | Kind / cause | Handler | What it does |
|---|---|---|---|
| `004A03C0` (renderer init, game thread) | INTC 2 VBlank start | `004AB430` | frame counter + wake the VSync waiters (section 4) |
| `004A03C0` | INTC 5 VIF1 | `004AB548` | VIF1 interrupt (`VIF1_STAT`/`VIF1_CODE`): counts `i`-bit interrupts, restarts VIF1 (`FBRST`), may stop the GIF DMA (`0x1000A000` CHCR) |
| `004A03C0` | INTC 0 GS | `004AB668` | writes `GS CSR = 2` (clears FINISH), counts, calls `004AB930` on the second |
| `004A0D98` | DMAC 0, 1, 2 (VIF0, VIF1, GIF) | `004AB6D8` | DMA end: updates the channel flags (scratchpad `S+0x1C+12n`, `S = 0x70002050`), continues multi-part transfers, switches VIF1/GS to GS->EE download when one is queued (`004AB930`/`004AB9C0`: `VIF1_STAT.FDR`, GS `BUSDIR 0x12001040`, VIF1 DMA `0x10009000`), and wakes the threads waiting on that channel (list `S+0x20+12n`); the channel semaphores (`S+0x24+12n`) are waited on by `004A0F08` |
| `00551758` (pad thread) | INTC 2 | `00551728` | `iSignalSema` -> pad thread reads both pads |
| `0054A230` (memory-card poll thread) | INTC 2 | `0054A1D8` | `iSignalSema` (at most 2 pending) -> card status poll |
| `0055EB50` (sound thread) | INTC 2 | `0055EB08` | `iSignalSema` (at most 2 pending) -> `00547B58` + `00568080` sound/stream update |
| `005552B8` (Logitech thread) | INTC 2 | `00555290` | updates the wheel state `005554B0` |
| `00557B18` (generic VBlank hook thread) | INTC 2 | `00557AF0` | `iSignalSema` -> calls a hook |
| `005034B0` (network) | INTC 2 | `00503688` | network timer, `iSetTimerAlarm` |
| `0054C8F0` (movie) | INTC 8 IPU; DMAC 3 fromIPU; `SetAlarm(1000, 0054D0B8)` | `0054D070`, `0054CFA8` | MPEG decode via the IPU |
| `0054D638` (movie) | INTC 8; DMAC 3 | `0054D960`, `0054D9A0` | second IPU decoder |
| `005202F0` (network module upload) | INTC 0-10 | `00520968` (returns 0) | placeholder handlers |
| SDK `InitTimer` | INTC 11 timer 2 | `cbTimerHandler 005B8158` | SDK alarms and `GetTimerSystemTime` |
| SDK `sceSifInitCmd` | DMAC 5 SIF0 | `_sceSifCmdIntrHdlr 005B0E30` | SIF commands from the IOP (RPC replies) |

Handlers follow the kernel convention: return non-zero to stop the chain, end with `ExitHandler`
(`sync; ei`) and only call `i*` kernel functions.

## 4. Frame timing [V]

All the game's pacing hangs on **INTC 2 (VBlank start)**, handler `004AB430`, state in the
scratchpad at `S = 0x70002050`:

- `S+0x00` (u64): VBlank counter, `+1` each VBlank.
- `S+0x10`: optional hook `fn(int counter)`, called from the handler if set.
- `S+0x14` (u8): GS `CSR.FIELD` (bit 13 of `0x12001000`) at this VBlank (odd/even field).
- `S+0x40`: list of threads waiting for the next VBlank (`{next, thread id}` nodes on their
  stacks); the handler `iWakeupThread`s each of them; then the three DMA-channel waiter lists at
  `S+0x20`, `S+0x2C`, `S+0x38` are woken the same way (so channel waits time out at VBlank).
- `S+0x48`/`S+0x50` (u64): accumulated performance counters.

Waiting: `004A21F0` (VSync wait) pushes the calling thread on `S+0x40` under `DIntr`, then
`SleepThread()` until the counter changes, then unlinks and `CancelWakeupThread`. `004A22C0(n)`:
`n > 0` waits `n` VBlanks; `n < 0` waits until `-n` VBlanks have passed since the previous flip
(returns 1 if it did not have to wait); `n == 0` returns the counter.

The game thread `001016E8` loop: `004A0690` (take the render lock at `S+0xC0`), `001077F8` (swap
the two packet buffers, kick DMA), `00101400`, `00107840`, `004A06F0` (release), game update,
`00576788`/`005767C0` lock around the object switch, then `00107878`: renderer end-of-frame
(`004AA4A0`, `004A1C98`), raise own priority to 1, `004A20C8`, **`004A22C0(-D_006186E4)`** (or
`-1` when `D_006186DC`), flip display buffers, restore priority. `D_006186E4` is the flip cycle
in VBlanks: initial value 1 (a new frame every VBlank, 59.94 Hz), set by `00107AB8`, read by
`00107AC8` (`PlayStation3::RenderLoopGetFlipCycle` in GT HD's naming); `D_006186DC` (set by
`00107AA0`) switches to "wait one VBlank".

Other VBlank consumers: pad, memory-card poll, sound, Logitech, generic hook (section 3.1), all
through a semaphore signalled by their own INTC 2 handler. The SDK `VSync()`/`VSync2()` are not
called.

**Host**: deliver one "INTC 2" event per 1/59.94 s (NTSC interlaced, `sceGsResetGraph(0, 1, 2,
1)`), running every registered INTC 2 handler in registration order with other guest threads
stopped (interrupt context: `DIntr` excluded), toggling the CSR FIELD bit read at `0x12001000`
each time; INTC 3 (VBlank end) is only used by liberx. GS FINISH (INTC 0) and VIF1 (INTC 5)
must also be raised when the renderer seam completes the corresponding packets, and DMA-end
(DMAC 0/1/2) when a VIF/GIF transfer is consumed, or the render thread will block on its
channel semaphores.

## 5. SIF RPC servers

### 5.0 How the game calls its servers [V]

The PDI client object (`src/libstream/unit_005676B0`, class `PDIClientRpc` vtable `00689E60` and
subclasses): `+0` a busy semaphore, `+4` the `sceSifClientData`, `+0x2C`/`+0x30` send pointer
and size (the class's fixed capacity: **the whole send area is sent every call**), `+0x34` async
flag, `+0x38` vptr, `+0x40` inline send area. A call: `WaitSema(+0)`, fill the send area,
`00578168(obj, fno, mode, recv, rsize)` -> `00579D30` (`_sceSDC` on send and receive buffers)
-> `sceSifCallRpc(obj+4, fno, mode | SIF_RPC_M_NOWBDC, send, ssize, recv, rsize, end 005780F8,
obj)`, retried while it returns non-zero. Synchronous calls signal `+0` on return; asynchronous
ones (mode bit 0) complete in `005780F8` (virtual slot 2, then `SignalSema(+0)`), so each object
has at most one call in flight. Receive buffers are 64-byte aligned. Bind: `00578148(obj, id)`
-> `00577FC8`, `sceSifBindRpc` + 2 ms delay until the server pointer is set.

Server IDs bound by the EE (all verified at the bind sites):

| ID | Name | Module (IOP side) | EE object | Bound in | Host service |
|---|---|---|---|---|---|
| `0x50434456` | `PCDV` | PDICDVD | `0086CB80` | `00548500` (boot B2) | disc sectors |
| `0x50636476` | `Pcdv` | PDICDVD | `0086CC80` | `00548500` | disc control/status |
| `0x53545250` | `STRP` | PDISTR | `0086F800` | `00550D00` (B4) | file streams |
| `0x5042474D` | `PBGM` | PDISTR | `0086CA40` | `00547D88` (B4) | music |
| `0x564F4943` | `VOIC` | PDISTR | `0086F8C0` | `00551218` (B4) | voice PCM |
| `0x4D504731` / `0x4D504732` | `MPG1` / `MPG2` | PDISTR | `0086F640` / `0086F700` | `0054F340` (B4) | movies |
| `0x53505550` / `0x53505554` | `SPUP` / `SPUT` | PDISPU2 | `008735C0` / `008739C0` | `0055E5C8` (B4) | SPU2 |
| `0x62737550` | `bsuP` | PDIUSB | `0064CCC0` | `00554180` (B5) | USB manager |
| `0x424B5550`, `0x534D5550`, `0x54485550`, `0x45535550` | `BKUP` `SMUP` `THUP` `ESUP` | PDIUSB | via `00553A78` | on device activation | USB keyboard, mouse, HID, serial |
| `0x50555354` | `PUST` | USTORAGE | | `00554258` (B6) | USB storage |
| `0x046D046D` | (Logitech) | LGDEV | `00873F00` | `00560778` (B7) | wheel |
| `0x504E4346` | `PNCF` | PDINETCNF | | `004ED1F8` | network config |
| `0x80000001` | fileio | FILEIO (IOPRP) | `008899C0` | `sceFsInit` | files (only host/debug paths) |
| `0x80000003` | iopheap | FILEIO (IOPRP) [I] | `00889AC0` | `sceSifInitIopHeap` | IOP heap (network upload) |
| `0x80000006` | loadfile | LOADFILE (IOPRP) | `00889E80` | `_lf_bind` | module loading |
| `0x80000400` | mcserv | MCSERV | `0087E480` | `sceMcInit` | memory card |
| `0x80000592`/`593`/`595`/`597`/`59A`/`59C` | cdvdfsv | CDVDFSV (IOPRP) | | libcdvd | only `sceCdInit`/`sceCdMmode` at boot |
| `0x80001300`, `0x8000131C`, `0x8000131E`+ | dbcman | DBCMAN | `00882DC0`... | `sceDbcInit` | pad (libpad2) |
| `0x80001400` | eyetoy | EYETOY | `0087E3D0` | `0058BCE8` | camera (on demand) |
| `0x80001201` | msifrpc | RTINETB | | `0056F9B8` (`sceSifMBindRpcParam`) | network |
| `0x04B80005` | Epson | EPSPRINT (on demand) | | printing | printer |

The IOP also calls the EE: `BSUP` `0x42535550` (EE server, section 5.7) and `0x04B80006` (the
printer driver's EE server, ERX side).

### 5.1 `PCDV` / `Pcdv` (PDICDVD) -- disc sectors [V]

IOP side: a priority-sorted job queue in front of `cdvdman`'s `sceCdRead` (2048-byte sectors,
spindle 0, one try per call; retried forever when the job asks). Jobs of priority 1 (PCDV reads,
STRP) pre-empt priority-2 streams (PBGM/MPG) through "hold" tokens. The IOP never translates
LBAs: the EE always sends **absolute** sector numbers in the linear dual-layer image. A disc
check (at most once a second, or after an error: tray state, disk type 0x12-0x14, checksum of a
configured sector) sets a drive-state word.

`PCDV` (send area `+0x80`, 64 bytes; receive `+0xC0`, 64 bytes; global issue lock `D_0064C3C8`):

| fno | EE sender | Send | Reply | Mode | Server action |
|---|---|---|---|---|---|
| 0 | (dead copy `0060FE40` only) | | `{0x111}` | | version |
| 1 | `005485B0` <- `00549228` (after a failed ISO lookup) | | | async | disable the disc-identity check |
| 2 | `005485D8` <- `00548F68` | `+0` LBA 16 (layer-0 PVD), `+4` checksum sum(byte[i]*(i+1)) of that sector (`00548D20`) | | async | set the disc-identity check `{lba, sum}` |
| 3 | `00548660`/`00548718` via `00548640(dst, lba, nbytes, flag)` | `+0` absolute LBA, `+4` byte count (multiple of 64, not of 2048), `+8` EE destination (64-aligned), `+0xC` u8 retry-until-success (0 for volume descriptors, 1 for directories/VOL header/TOC) | `+0` u32 failed (0 = OK) | async; the EE then waits on the done semaphore `+0x40`, signalled by the end callback `00548428` | read `ceil(n/2048)` sectors, copy `n` bytes to EE memory (the IOP streams them through a 128 KB FIFO in 64 KB SIF DMAs) |
| 4 | `005487C0` <- `004ACB70` | | `+0` on_dual, `+4` layer-1 start LBA | sync | `sceCdReadDvdDualInfo`; the EE loops until `+0` != 0 |

Largest read seen: the layer-0 TOC, `0x58680` bytes, in one call (no chunking on the EE side).

`Pcdv` (control; exists because PCDV blocks during reads):

| fno | EE sender | Send | Server action |
|---|---|---|---|
| 0 | `00548500` at boot | `+0` EE address `0x0086CD00` | start a thread that writes 16 bytes there **every 100 ms** by SIF DMA: `+0` u32 seconds since 2000-01-01 (RTC read once + IOP uptime), `+4` RTC status, `+8` drive state (0 OK, 1 read error, 2 not a PS2 disc, 3 tray check, 4 wrong disc [meanings I]), `+0xC` seconds since the state changed |
| 1 | `00548588` (no live caller) | | stop the pusher |
| 2 | `00548798` (no caller) | | cancel the current PCDV read (it returns failed = 1) |

The status block is the game's **clock**: `005488A8` reads `+0` (adds `0x31510F5B40`), `005488F8`
subtracts 9 h, `00548918` adds the time-zone offset computed at boot (`005524B8`), `005492C8`
builds a BCD `sceCdCLOCK`-style record. `00548850` reads `+8`/`+0xC`; `00104150` raises the
disc-error state when `+8 != 0 && +0xC >= 3`. **Host**: write `{now - 2000-01-01 in s, 0, 0, 0}`
to `0x0086CD00` periodically (or before each read of it).

### 5.2 `STRP` (PDISTR, "pdipipe") -- file streams [V]

Clients: the PDISTD file devices (`FileDevicePipe` vtable `00689050`, `FileDeviceRo` `00689178`,
`FileDeviceRo2` `00689268`, `FileDeviceRo2ExDL` `00688C58`). Send area 0x80 bytes. Handles are
opaque words the EE echoes back.

| fno | EE wrapper (caller) | Send | Reply | Server action |
|---|---|---|---|---|
| 0 | (dead copy) | | `{0x107}` | version |
| 1 | `00550EE8` (`Pipe::stat 004B0CD0`, host mode) | `+0` path | `{not_found, compressed, size, unpacked_size}` | stat; compressed = file starts with `0xFFF7EEC5`, unpacked = -second word |
| 2 | `00550E90` | `+0` handle | | close |
| 3 | `00550D28` -> `00550B88` (`Ro::open 004B1950`, `004B1E34`/`004B1E8C` host TOC, `004B0FD0`) | `+0` absolute LBA (disc) or byte offset (host file), `+4` size, `+8` 0x8000 (max IOP buffer), `+0x14` path (empty = disc) | `+0` handle (0 = failure) | open a read stream on the disc via PDICDVD (priority 1, hold token) or on a file |
| 4 | `00550D88`/`00550DB8` (`Pipe::read 004B0FD8`, ioctl kind 1) | `+0` handle, `+4` EE address (64-aligned), `+8` byte count (multiple of 64) | none | blocking sequential read into EE memory; the EE invalidates the range afterwards |
| 5 | `00550DE8` (ioctl kind 2, sound-bank loader `0044D540`) | `+0` handle, `+4` **SPU2 RAM address**, `+8` size | none | stream the next bytes of the file straight into SPU2 RAM (`sceSdVoiceTrans` write) |
| 6 | `00550D58` (`Pipe::open 004B0E28`, write) | as fno 3 (`0, -1, 0x8000, path`) | handle | open a write stream (host files only) |
| 7 | `00550E60` (`Pipe::write 004B11A8`, flush in close) | as fno 4 | none | write from EE memory |
| 8 | `00551010` (no caller) | path | | no-op |
| 9 | `00551070` (ioctl kind 3, voice opener `00557F90` with `0x7FFFFFFF`) | `+0` handle, `+4` flag | | if flag: release the disc hold token (lets lower-priority streams run) |

Reads are strictly sequential (no seek command; the device's seek slots return "unsupported");
unaligned heads/tails go through a 64-byte bounce read, so **the last read can ask up to 63
bytes past the stored size**: the server must return them (zeros or the following sectors).
Units of fno 3 `+4`: the EE passes the stored size in **bytes** in disc mode (`Ro::open`); the
IRX analysis read the disc-mode field as a sector count. Either way a host that serves whatever
the reads ask for is safe (open question 3).

Sound banks [V]: `0044D540` (`FileInstrumentStream`) reads the 0x20-byte `INST`/`ENGN` header and
tables into EE memory (fno 4), then sends fno 5 with SPU address `D_00623A38` (0x1E0000 / 0x5040
/ 0xA7040, set by `00463000`/`00462EC8`) and size = header `+0x10`.

### 5.3 `PBGM` (PDISTR) -- streamed music [V]

The EE never touches the audio: it passes the file's absolute LBA, size and loop flag, and the
IOP streams and plays it. RPCs are sent by the BGM worker thread `00547550` from flags set by the
API (`0086C8E0`); the server posts each command to its own worker and returns at once. Send area
0x80 bytes.

| fno | EE sender | Send | Reply | Server action |
|---|---|---|---|---|
| 1 | `00547E10` (`0045FFF8` -> `00547860(path, mode, buf)`) | `+0` start LBA (from the file's ioctl-0 stat), `+4` size in bytes, `+8` u8 loop, `+0xC` 0x40000 (IOP buffer), `+0x10` name (empty on disc) | | sync | play: parse a 0x28-byte header (10 LE words: word 2 codec 1 = 16-bit PCM, 2 = SPU ADPCM mono, 0x10 = SPU ADPCM stereo interleave 0x400; word 3 sample rate; word 9 data bytes), stream data, loop |
| 2 | `00547F50` (before every play) | | | stop |
| 3 | `00547F78` | `+0` s32 clamp(x,-1,1)*32768 | | fade (10 ms steps to 0 or 0x7FFF; reaching 0 stops) |
| 4 | `00548060` (volume) | `+0` s32 clamp(v,-1,1)*32768 | | master volume |
| 5 | `005480F0` (no caller) | `+0` s32 clamp(v,0,1)*32768 | | fade volume |
| 6 / 7 | `00547F00` / `00547F28` | | | pause / resume |
| 8 | `00548178`, **every VBlank** from the sound thread (`00547B58`) | | 0x40 bytes into `0086CB00` (copied to `0086CB40`): `+0` s64 position in microseconds, `+8` s64 sample counter, `+0x10` loop count, `+0x14` finished | status |
| 9 | `00548008` (`SystemSoundSetOutputMode 00462878`) | `+0` s32 (output mode == 0) | | mono flag |

Files: `/bgm/*.ads` in the volume; when a buffer is given the EE also loads the companion `.adm`
(metadata) itself through the file system (`004AE370`, parsed by `0045FD48`).

### 5.4 `VOIC` (PDISTR) -- narration PCM [V]

The licence-test narration is Speex, read and decoded **on the EE** (`CustomSpeexPlayer` vtable
`00689A60`, `CustomSpeexDecoder` `00567920`, 0x800-byte reads into a 0xA000 ring); the IOP only
plays PCM.

| fno | EE sender | Send | Reply | Server action |
|---|---|---|---|---|
| 1 | `00551240` (player ctor `00557C70`) | `+0` -1 (any SPU DMA channel), `+4` 0x2800 (half size) | `+0` handle | create a PCM output (ADMA, FIFO 2 x half) |
| 2 | `005512C0` | `+0` handle | | destroy |
| 3 | `00551318` (`00558100`, every ~53.3 ms) | `+0` handle, `+4` EE address of 0x2800 bytes, `+8` 0x2800 | | pull the block from EE memory (`sceSifGetOtherData`) and queue it; **the reply is the pacing** (it returns when there is room) |
| 4 / 6 | (no callers) | `{handle, speed}` / `{handle, vol}` | | fade / fade volume |
| 5 | `00551400` (`005584D0`) | `+0` handle, `+4` volume & 0x3FFF | | volume |

Format: 16-bit PCM, 48 kHz [I from the 53.3 ms period], in SPU2 ADMA layout (512-byte L block,
512-byte R block; the EE duplicates the mono 0x200-byte blocks into L and R).

### 5.5 `MPG1` / `MPG2` (PDISTR) -- movies [V]

The IOP reads the PSS file, demuxes the MPEG-2 program stream, plays the audio (private stream 1,
PCM via ADMA or SPU ADPCM) and hands the **video PES payloads** to the EE, which decodes them
with the IPU (`mpegif`, `0054DF90` preload / `0054E3D8` streaming, read thread `0054E948`).

| Server, fno | EE sender | Send | Server action |
|---|---|---|---|
| MPG1 1 | `0054F388` | `+0` flags (bits 0-3 audio substream, 0x10 loop, 0x20 PCM track, 0x40 ADPCM track), `+4` start LBA, `+8` size in sectors (`(size + 2047) >> 11`), `+0xC` name (empty on disc) | start demux (256 KB FIFO), sync |
| MPG1 2 | `0054F420`, repeatedly | `+0` EE buffer, `+4` 0xA000 | blocks until filled: 8 slots of 0x1400 bytes, each `{u32 len (< 0x13F0), u32 misalign, u32 remaining, 0}` + payload (16-byte rounded); a zero header ends. The EE rewrites each slot into a PES packet `00 00 01 E0`, an invalid one into the end code `00 00 01 B9` |
| MPG2 3 | `0054F5E0` (`001FE018`, `001FE558`), async | `+0` s64 t0, `+8` s64 t1 (microseconds, e.g. frame x 33366.05 or `0, INT64_MAX`) | set the audio clock base / play window |
| MPG2 4 | `0054F648` (end of every movie) | | stop |

Files: `/mpeg/...pss` on the layer-1 volume (`/gt4l1.vol`). A host can either serve this
protocol (demux + audio + PES slots) or replace the player at the `mpegif` level.

### 5.6 `SPUP` / `SPUT` (PDISPU2) -- the SPU2 [V]

The sound driver (SDDRV: sequencers `.sqt`, instruments `.ins` `INST`, engine sounds `.es`
`ENGN`) runs **on the EE**; the IOP only applies register changes. Every VBlank the sound thread
(`0055EB50` -> table `00568080` slot 3 `0055DDB8` -> `0055DF70`) sends **SPUP fno 4,
asynchronously**, with the whole send area: 2 cores x 0x1D0 bytes (+ 0x18 unused), replying 0x80
bytes.

Per core (layout from the IOP handler, defaults from the EE initialiser `0055E948`):

| Offset | Field |
|---|---|
| `+0` | u32 core flags: 0x2 EVOL, 0x4 AVOL, 0x8 BVOL, 0x10 PMON, 0x20 NON, 0x40 VMIXL, 0x80 VMIXEL, 0x100 VMIXR, 0x200 VMIXER, 0x400 MMIX, 0x800 KON, 0x1000 KOFF, 0x2000 noise clock |
| `+4` | u32 voice update mask (24 bits) |
| `+8 + 16v` (v = 0..23) | u16 flags (0x01 pitch, 0x02 volL, 0x04 volR, 0x08 start address, 0x10 ADSR1, 0x20 ADSR2, 0x40 key-on reset), u16 pitch, u16 volL, u16 volR, u32 SSA (SPU RAM byte address), u16 ADSR1, u16 ADSR2 |
| `+0x18C` / `+0x190` / `+0x194` | EVOL / AVOL / BVOL L,R |
| `+0x198` `+0x19C` | PMON, NON |
| `+0x1A0` `+0x1A4` `+0x1A8` `+0x1AC` | VMIXL, VMIXEL, VMIXR, VMIXER |
| `+0x1B0` | MMIX (0xFC0 core 0, 0xFCC core 1) |
| `+0x1B4` / `+0x1B8` | KON / KOFF masks |
| `+0x1BC` | noise clock |
| `+0x1C0`.. | effect: set flag, enable, clear work area, mode, depth L/R, delay, feedback, end address (0x1DFFFF / 0x1FFFFF) |

Reply (the IOP's command-3 status, 0x68 bytes used): per core `u32 ENDX` + `u16 ENVX[24]`. The EE
reads ENVX (`0055E6A8`/`0055E678` in `0055D890`) and **releases a voice whose ENVX reads 0**, so a
host must return non-zero envelopes for sounding voices or every sound stops after one frame.

Other commands: SPUP 5 `{1, 1, 1}` once from `00462808` (S/PDIF config: enable, DVD media, copy
prohibit); 0 version `{0x112}`; 3 status; 6 bitstream flag (no live callers). SPUT 1/2 (SPU RAM
<-> EE `{spu_addr, ee_addr, size}`) have no callers: sample data reaches SPU RAM through STRP fno
5. SPU RAM map used: banks at 0x5040 / 0xA7040 / 0x1E0000, BGM ADPCM rings at 0x1D8000 /
0x1F8000 (voices 22-23 of core 1), effect work areas below 0x1DFFFF / 0x1FFFFF.

**Host**: a 2 MB SPU RAM image (filled by STRP fno 5 and PBGM's ADPCM player) and an SPU2 voice
mixer (ADPCM decode, pitch, ADSR, volumes, reverb), fed by this packet every frame, plus the BGM,
VOIC and movie PCM streams mixed in.

### 5.7 USB: `bsuP`, `BSUP`, class servers, `PUST` [V]

- `bsuP` `0x62737550` (PDIUSB): fno 0 at init (`00554180`) makes the IOP bind the EE server
  `BSUP`; fno 1-4 (= device class, `00565AD0`) start the class servers `BKUP` (keyboard), `SMUP`
  (mouse), `THUP` (HID output), `ESUP` (serial), which the EE binds (`00553A78`) and drives with
  `00565C30` messages `{op, val, len, data[]}`.
- `BSUP` `0x42535550` is served **by the EE**: thread `00553B80` (priority 12) does
  `sceSifSetRpcQueue`, `sceSifRegisterRpc(0x42535550, dispatch 00553D80, receive buffer 00870000
  [0xC0])`, `sceSifRpcLoop`. The IOP sends `{port, type (1 attach, 2 detach, 5 data, 6 echo),
  slot, ..., size +0x18, data +0x1C}`; fno 1/2/4 are queued to thread `00553C08`, 3/5 handled at
  once; no reply.
- `PUST` `0x50555354` (USTORAGE): fno 0 version, 1 device info, 2 LUN op, 3 capacity, 4 sector
  transfer `{handle, lun, lba, ee_addr, bytes, dir}`, 5/6 driver register; the FAT layer is on
  the EE (`PlayStation2::UsbStorage`), used by photo export.

**Host**: answer the binds and fno 0, never send attach messages: no USB device appears.

### 5.8 Logitech wheel `0x046D046D` (LGDEV 1.11.036) [V]

Client in `src/libstream/unit_005547E8` (`00560778`-`00561D10`), driven by the VBlank thread
`005552B8`. Send and receive share one 0x240-byte buffer (`00873F40`), processed in place; result
code at `+0` (0x80000002 bad index, ...04 bad handle, ...05 already open, ...06 no device).

- Init `00560778`: bind, then **fno 12** with `+0x5C` = uncached EE address of `00874300`: the IOP
  writes 64 bytes there (first byte 1) and replies `+4` = version; the EE accepts `0x010B2400` or
  any `0x010Bxxxx` (else it hangs). Then fno 18 if a saved state exists.
- Device commands: 1 device info (index `+4` -> 0x18 bytes at `+0x70`), 2 open (handle `+8`),
  3 close, 4/5 get/set property (`+0x58` id, `+0x5C` value), 6/7 read input state (to `+0x70`),
  8 download a force effect (`+0x18` params -> effect handle `+0x70`), 9/10/13/14 effect
  start/stop/update, 15/16 effect parameters, 17/18 device scan.
- Supported: VID 0x046D, PID 0xC293-0xC295, 0xC298 (Driving Force Pro), 0xCA00-0xCA7F.

**Host**: reply to fno 12 with version `0x010B2400` and report no device (fno 1 -> 0x80000006),
or map an SDL wheel with force feedback onto these commands.

### 5.9 Network, EyeToy, printer (brief) [V]

- `PNCF` `0x504E4346` (PDINETCNF, loaded on demand after `sceSifSearchModuleByName("PDI_NETCNF")`):
  network configuration entries on the memory card (count, list, load, add, edit, delete).
- `0x80001201` (RTINETB libnet over MSIFRPC): sockets; `sceSifMInitRpc` at boot (C2).
- `0x80001400` (EYETOY.IRX, loaded on demand by `001C56B0`).
- `0x04B80005` (EPSPRINT.IRX, via the EE ERX drivers in `cdrom0:\EPSON\`, `sceErxInit` +
  `sceOpen`/`sceRead` through the standard `cdrom0:` driver): photo printing.

None is needed to boot or race offline; the binds happen only when the feature is used, except
msifrpc's init (`sceSifMInitRpc`) and the SMAP load predicate.

### 5.10 Sony SDK servers (standard protocols)

libcdvd's `sceCdInit(0)`/`sceCdMmode(2)` at boot and `sceCdInit(5)` at exit are the only libcdvd
calls (no `sceCdRead`, no `sceCdSearchFile`, no `sceCdSt*`/`sceCdda*` callers) [V]. fileio
(`sceOpen`...) is used only by `host:` storage (`mStorageHD` `00242808`-`00242CC0`), debug dumps
(`00332AA8`...) and the liberx/ROM paths [V]. libmc, libdbc/libpad2: sections 7 and 8.

## 6. Disc and file access [V]

**The EE never reads the disc through `sceCd*` or `cdrom0:`.** It mounts the game's two volumes
with its own ISO9660 reader over PCDV, then reads every file through STRP (or lets PBGM/MPG1 read
by LBA). Every LBA sent to the IOP is absolute in the linear disc image.

### 6.1 Disc layout (ISO9660/UDF, dual layer)

| File | LBA | Size | Use |
|---|---|---|---|
| `SYSTEM.CNF` | 429 | 57 | `BOOT2 = cdrom0:\SCUS_973.28;1`, `VER = 1.01`, `VMODE = NTSC` |
| `SCUS_973.28` | 430 | 273,020 | boot loader (section 1.1) |
| `CORE.GT4` | 564 | 2,020,861 | the game, compressed (= `CORE.GT4.elf`) |
| `IOPRP300.IMG` | 1551 | 278,465 | IOP replacement modules: SYSMEM, LOADCORE, SIFCMD, SIFMAN, THREADMAN, IOMAN, MODLOAD, FILEIO, CDVDMAN, CDVDFSV, LOADFILE, TIMEMANI, ROMDRV, EESYNC, SYSCLIB, STDIO (build 2004-10-29) |
| `IRX/*.IRX` | 1687-2059 | 1.5-144 KB | 27 IOP modules (23 loaded at boot) |
| `NET/` | | | `ICON.SYS`, `SYS_NET.ICO` (netcnf.irx arguments) |
| `EPSON/` | | | printer ERX drivers and `.RES` profiles |
| `GT4.VOL` | 105879 | 2,459,506,688 | layer-0 volume: all game data |
| `GT4L1.VOL` | layer-1 relative 267 (absolute 1,317,307) | 2,595,657,728 | layer-1 volume: `/mpeg` only |

Layer 1 starts at absolute LBA **1,317,040** (PCDV fno 4); its own PVD is at 1,317,056. The
root-only listing of the layer-0 directory does not show `GT4L1.VOL`.

### 6.2 Mounting [V]

Two static `FileDeviceRo2ExDL` objects (ctor `004ACC28`, from the static constructor `00100D10`):
`00617AB0` = (index 0, skip `0x2159`, `"/gt4.vol"`, prefix `/`, priority 1) and `00617BB0` =
(index 1, skip `0x2159`, `"/gt4l1.vol"`, names `{"/mpeg"}`, priority 0). Chain: ExDL `004ACA40` ->
Ro2 `004B1C10` -> Ro `004B1600` -> Pipe `004B0960` -> `PDISTD::FileDevice` `004AD1C8`. The
layer-1 device answers paths under `/mpeg`, the layer-0 device everything else.

Mount (`004ACB18` -> `004B1C70`), at boot from `004ACC68`:

1. Layer 1 only: `ec` = PCDV fno 4 until non-zero (layer-1 start).
2. `00549130(path, layerBase)`: read volume descriptors from `layerBase + 16` (PCDV fno 3 through
   the 6-sector LRU cache `00548A80`), take the PVD root, walk the path components
   case-insensitively (`;1` appended to the last), extent LBA + `layerBase`. On failure: clear the
   cache, PCDV fno 1, retry.
3. `base = extent + 0x2159` (8537 sectors: the volume's first sectors hold a decoy GT3-style
   TOC the game never reads). GT4.VOL: 105879 + 8537 = **114,416**; GT4L1.VOL: **1,325,844**.
4. PCDV-read 64 bytes at `base` (header), then the whole TOC (`(hdr+8 + 0x3F) & ~0x3F` bytes).
5. `PageManager::init` `004B3840`; page pool of 8 pages.

### 6.3 The VOL format (RoFS) [V, checked on the disc]

Header at `base` (64 bytes):

| Offset | Field | GT4.VOL |
|---|---|---|
| `+0` | magic: `0xACB990AD` (obfuscated) or `0x53466F52` "RoFS" (plain), test at `004B38A0` | obfuscated |
| `+4` | version | `0x00030001` |
| `+8` | TOC size in bytes, header included | `0x58642` |
| `+0xC` | data start, in sectors from `base` | `0xB1` (177) |
| `+0x10` | u16 page size | `0x800` |
| `+0x12` | u16 page count | 407 (GT4L1: 3) |
| `+0x40` | u32 page offsets, count + 1 | |

- Page `i` = TOC bytes `[off(i), off(i+1))`, with `off(i) = word(0x40 + 4i) XOR (i+1)*0x14AC327A`
  in the obfuscated variant (`004B3938`; plain `004B38F8`).
- A page is raw deflate (no zlib header) inflating to `pageSize` bytes (`FileDeviceRoInflator`
  `004B1B10` -> `004B0120`), then every byte XOR `0x55` in the obfuscated variant (`004B36E0`).
- Node: u16 tag (1 index, 0 leaf), u16 count, s32 next leaf (-1 none); slot `k` (8 bytes) at
  `pageSize - 8k - 8`: u16 key offset, u16 key length, u32 value offset (leaf) or child page
  (index). Root = page 0. Search: index `004B2518`, leaf `004B27D0` (binary, then linear under 8),
  key compare `004B2488` (memcmp, shorter first).
- Key = big-endian u32 parent directory id + name bytes (lower case, no NUL); root id 1. Leaf
  value: type 0 directory `{dirId}`; type 1 file `{sector, mtime, size}`; type 2 compressed file
  `{sector, mtime, storedSize, realSize}`.
- Path lookup: `004B3350` (normalise: drop leading `/`, `.`, `..`) -> `004B3270` (per component
  `004B30B8`) -> Ro2 slot 28 `004B1F90`: **LBA = base + hdr[+0xC] + sector**, stored size,
  real size, compressed flag. Example: `sound/gt4sys.ins` -> sector 695 -> LBA 115,288.
- Compressed files are stored as `u32 0xFFF7EEC5, s32 -realSize`, then raw deflate; the **EE**
  inflates them (checks at `0048F2E0`, `00218AD0`, `001F7B58`). The IOP only delivers stored
  bytes.
- Top-level directories of GT4.VOL: advertise, bgm, cameras, car, character, config, crs,
  description, dnas, eyetoy, fep, font, icon, menu, music, narration, photo, piece, pit,
  printer, projects, race, race_display, replay, rtext, script, sound, specdb, text, tire,
  usedcar, wheel, wing. GT4L1.VOL: mpeg.

### 6.4 Reading a file [V]

`Pipe::open` `004B0E28`: TOC lookup -> `{status, compressed, realSize, storedSize, -, LBA}` ->
`Ro::open` `004B1950` -> STRP fno 3 `(LBA, storedSize, 0x8000, "")` -> handle. `Pipe::read`
`004B0FD8`: STRP fno 4 with 64-aligned destination and 64-multiple sizes, bounce buffer for the
edges. Close: STRP fno 2. Music and movies: the file's `{LBA, size}` (ioctl kind 0, answered from
the TOC on the EE) go to PBGM fno 1 / MPG1 fno 1, which read the disc themselves.

### 6.5 What the host disc service must do

1. **Sector read** (PCDV 3): copy `n` bytes from image offset `LBA * 2048` to EE address `dst`,
   reply `{0}`, complete the async call (so the end callback signals the waiting thread).
2. **Layer-1 start** (PCDV 4): reply `{1, 1317040}` (from the image's layer-1 PVD position).
3. PCDV 1, 2: acknowledge.
4. **Clock / drive status** (Pcdv 0): keep writing `{seconds since 2000-01-01, 0, 0, 0}` at the
   given EE address.
5. **Streams** (STRP 3/4/2): a handle per open with a sequential cursor at `LBA * 2048`; fno 4
   copies the next `n` bytes (allowing reads up to 63 bytes past the end); fno 5 copies into the
   SPU RAM image; fno 9 acknowledge.
6. PBGM 1 / MPG1 1: open a reader at `LBA * 2048` for `size` bytes / `sectors * 2048`.

No other disc access exists at run time except the printer ERX drivers (`cdrom0:\EPSON\`, via
fileio) and the IRX loads (which the host replaces).

## 7. Pad [V]

Library: libpad2 over libdbc (`DBCMAN.IRX`, `SIO2D.IRX`, `DS2U_D.IRX`; RPC `0x80001300`,
`0x8000131C`, `0x8000131E+n`), libvib for vibration. libpad2 receives the controller data by DMA
into 256-byte buffers in EE memory (`0086F980`, `0086FA80`); the host can implement the libpad2
functions directly.

Initialisation (`00551810`, boot A9, after `sceDbcInit()` returned 1):

- `while (scePad2Init(0) != 1) DelayThread;`
- two sockets: `scePad2CreateSocket({option = 2 (SPECIFIC_PORT), port = 0 / 1, slot 0, number 0},
  dma buffer 0x0086F980 + 0x100*i)`, retried until `>= 0`; the socket ids go to the port records
  at `0086FC80 + 0x6C*i`.
- the reader thread `00551758` (priority 13) installs INTC 2 handler `00551728` and, each VBlank,
  runs `00551580`.

Per VBlank (`00551580`, under the lock `0086F940`, priority raised to 1), for each port:

1. `scePad2GetState(id)`; when it is 1 (stable): on first contact `scePad2GetButtonProfile(id,
   +0x24)` (4-byte profile; the return value goes to `+0x20`) and `sceVibGetProfile(id, +0x10)`
   (vibration profile, 16 bytes; return to `+0x0C`).
2. `scePad2Read(id, +0x2C)`: the button data; its return value (bytes read) goes to `+0x28`.
3. If vibration is pending (`+0x40`), `sceVibSetActParam(id, len, profile, len, data)` from the
   record at `+0x44` (set by `00551B38`).
4. If the state is not 1, the record is cleared.

The game reads a port by copying 60 bytes from record `+4` (`00551A50`): state, status, mode,
vibration profile, profile result, button profile, read length, data.

Button data (libpad2, 18 bytes, the DualShock 2 layout) [V for the size and the index constants,
I for the byte order]: bytes 0-1 digital buttons, active low, bit = libpad2 index (0 Select,
1 L3, 2 R3, 3 Start, 4 Up, 5 Right, 6 Down, 7 Left, 8 L2, 9 R2, 10 L1, 11 R1, 12 Triangle,
13 Circle, 14 Cross, 15 Square); bytes 2-5 sticks RX, RY, LX, LY (0x00-0xFF, 0x80 centre);
bytes 6-17 pressures (0 = released, 0xFF = fully pressed) of Right, Left, Up, Down, Triangle,
Circle, Cross, Square, L1, R1, L2, R2 (indexes 20-31). The 4-byte button profile is the bitmask
of the indexes the device supports (all 32 bits for a DualShock 2). The game uses the pressure
bytes (accelerator/brake on Cross/Square by default [I]). An SDL gamepad fills buttons, sticks
and maps triggers to the L2/R2 pressures; faces to 0/0xFF unless the pad has analogue faces.

The Logitech wheel (Driving Force Pro / GT Force) does not go through libpad2: it is a USB device
served by `LGDEV.IRX` (server `0x046D046D`, section 5.8).

## 8. Memory card [V unless marked]

libmc (`MCMAN.IRX`/`MCSERV.IRX`, server `0x80000400`), called only from the memory-card manager
in `src/libstream/unit_005547E8` (`00561E00`-`00562738`, one wrapper per call, each under the lock
`00654D40` and retrying with `DelayThread` while the call returns < 0):

`sceMcInit` (`005621C0`, until it returns 0), `sceMcGetSlotMax`, `sceMcGetInfo`, `sceMcOpen`,
`sceMcClose`, `sceMcRead`, `sceMcWrite`, `sceMcSeek`, `sceMcSync`, `sceMcGetDir`, `sceMcMkdir`,
`sceMcDelete`, `sceMcRename`, `sceMcSetFileInfo`, `sceMcFormat`, `sceMcUnformat`,
`sceMcGetEntSpace`. Requests are queued to the worker thread `00549D98` (commands 1-13,
completion callbacks); `0054A230` polls the cards each VBlank (`0054A168`).

Names:

- Directory prefix `"BA" + "SCUS-97328"` = `BASCUS-97328` (`001CC958`); game data directory
  `BASCUS-97328GAMEDATA` (`001CDA48`), files `/%s/icon.sys`, `/%s/icon0.ico`, `/%s/thumbnail`
  [V strings]; other saves use `%s%04d` [I], photos `F%04d` / `P%04d`, play lists `LSTSLIDE`,
  `LSTRPLAY`. Icons are read from the volume (`icon/GT4_Gamedata.ico`, `icon/GT4_Rep_*.ico`...).
- Save kinds (classes `GT4MC::File*`): GameData, PatchData, Replay (Full, Best, License), Photo
  (Film, Picture), PlayList, SlideShowList, ReplayList, ReplayDemo (`DeviceDemo`, from disc).
- Other games' directories it looks for (data inheritance): `BASCUS-97102GAMEDATA`,
  `BESCES-50294GAMEDATA`, `BISCPS-15009GAMEDATA` (Gran Turismo 3), `BESCES-52438GAMEDATA`,
  `BISCPS-15055GAMEDATA`, `BKSCKA-20022GAMEDATA` (GT4 Prologue) (`001D4500`, `001D4598`; the
  game titles are inferred from the product codes).

Host: a directory per card slot (port 0/1) holding PS2-style directories and files, with libmc
semantics (async calls completed by `sceMcSync`, 1 KB clusters for `sceMcGetInfo`/
`sceMcGetEntSpace` free-space checks, `sceMcGetDir` entry structures with timestamps and
attributes). File sizes are not listed here (open question).

## 9. Open questions

1. **STRP fno 3 size unit in disc mode**: the EE (`Ro::open` `004B1950`) passes the stored size in
   bytes; the IRX analysis read the disc-mode field as a sector count. Check `PDISTR` `0x2C30`
   dispatch before relying on the server clipping streams to the file end.
2. **PBGM status fields** beyond the microsecond position (`+8`, `+0x10`, `+0x14`) and the `.ads`
   header fields other than codec, rate and length; the meaning of the `.adm` companion file.
3. **MPG1 slot header** field meanings (`misalign`, `remaining`) and the MPG2 fno 3 window
   semantics (two s64 microsecond times sent as four u32 to the audio clock).
4. **SPU2 packet**: the EE-side defaults name some fields differently from the IOP handler (core
   flag bits 0x800/0x1000 and the masks at `+0x1B4`/`+0x1B8`: the IOP reads KON then KOFF); the
   IOP layout is authoritative. Voice flag 0x40 (key-on reset) vs. the EE's use as key-off.
5. **Pcdv drive-state values** 1-4 and the RTC status word: meanings inferred; the host can
   always report 0.
6. **libpad2 byte order** inside the 18-byte data block: taken from the libpad2 index constants
   (DualShock 2 layout); confirm against `DS2U_D.IRX` or the game's readers of record `+0x2C`.
7. **Memory-card file sizes** and the exact directory names of replays/photos (`%s%04d` users not
   traced).
8. `004A22C0`'s other callers (`001060F8`, `00106380`, `001C8990`, `001FE2B8`, `mUpdateContext::Sync`)
   wait whole VBlanks during loading and movies; not traced further.
9. `005202F0` (network module upload) installs dummy handlers on INTC 0-10 and removes them; the
   purpose (probing handler slots?) is not established.
10. `0x80000003` (iopheap) is assumed to be served by FILEIO in IOPRP300.IMG (the constant is
    there; the handler was not traced). Only the network path uses it.
