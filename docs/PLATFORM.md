# Platform host: SDL3 + Vulkan

`gt4_host` (`platform/sdl/`, built with `-DGT4_BUILD_HOST=ON`) is the native platform layer the port
targets: SDL3 for the window, input, audio and timing, Vulkan for presentation and rendering. It links
no game code yet. Today it presents a diagnostic scene and proves the host plumbing end to end:

| Area | What works | How it is checked |
|---|---|---|
| Window / presentation | Vulkan device, swapchain, resize, minimize/restore, suspend recovery | `gt4_host --probe`, `gt4_host --smoke-test` |
| Input | SDL gamepad, including a scripted virtual gamepad | `tools/host_session.py run` drives `tests/scenarios/controller.json` |
| Audio | SDL playback, stereo 48 kHz, with a PCM WAV tap of what is submitted | session report (clipping, level) |
| Capture | GPU framebuffer readback to PPM/PNG per scripted frame | session report |

Run a session (Linux/WSL; on Windows, `tools/host_windows.ps1` starts it on the logged-in desktop):

```sh
cmake -S . -B build/host-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON
cmake --build build/host-linux
build/host-linux/gt4_host --smoke-test
python3 tools/host_session.py run --exe build/host-linux/gt4_host
```

## How the game will reach the host

The game code (from gt4-decomp) never talks to SDL or Vulkan directly. It keeps calling the PS2
services it was written against (GS/VIF packets, the IOP's pad, sound, disc and network RPCs), and
each of those calls lands in a shim that translates it to one host service:

| Host service | Replaces | Milestone |
|---|---|---|
| Video (Vulkan) | GIF/VIF packets, VU1 programs, GS state | R1 |
| Audio (SDL) | SPU2 / IOP sound RPC | A1 |
| Input (SDL gamepad) | IOP pad library (`scePad*`), pressure-sensitive buttons | P1 |
| Storage | disc (`sceCd*`), memory card | P1 |
| Clock | vsync, timers, threads | P1 |
| Net | IOP network stack (`sceInet*`, DNAS, lobby) | later |

## Network: ignored for now, designed in from the start

The retail game's network code (lobby, connection objects, the LAN battle mode, about 3,600 functions
around `unit_00494578` in gt4-decomp) is not ported yet. The host exposes a `NetService` interface
(`include/gt4/platform/net.hpp`) with a `NullNetService` that reports the adapter as absent, so the game
takes its offline paths, the same as a PS2 without a network adapter.

Multiplayer will come, so these rules hold from the first line of host code:

- **One simulation tick, owned by the game.** The game advances in its own 60 Hz frames; the host
  never runs game logic from its own timers or threads. Input is sampled once per tick and delivered as
  a per-tick record, which is also what deterministic replays (TEST-05) need. Lockstep or rollback
  netcode later replays exactly those records.
- **No hidden nondeterminism.** Game randomness, time and ordering come from game state or from a
  host service that can be recorded and replayed (Clock, Input, Net), never from wall-clock reads,
  pointer values or unordered containers in shims.
- **Rendering and audio only observe.** Video and Audio consume what a tick produced; nothing they
  do feeds back into game state, so a frame or audio buffer can be dropped, repeated or run at another
  rate without changing the simulation.
- **Net is message based and polled once per tick.** `NetService` sends and receives datagrams
  without blocking; the game shim polls it at a fixed point in the tick. Transport (UDP sockets, a relay
  server) is the host's choice behind the interface.
- **Network shims get their own seam.** When the network unit is ported, its IOP RPC calls go through
  the same shim pattern as the other services, so the LAN battle protocol can run over `NetService`
  without the game noticing the PS2 stack is gone.

## The renderer seam (R1)

`docs/HW_ACCESS.md` (`tools/hw_access.py`) lists every EE hardware register the game code reaches. The
graphics boundary is narrow: 53 game functions, all in the PS2 graphics library around 0x49F000-0x4AC000.
GIF packets leave through two DMA kicks (`004AB548`, `004AB6D8`), VIF1 uploads through two more
(`004AB930`, `004AB9C0`), and the display mode and framebuffers are set in one place (`004A0C10`). R1
starts by replacing those kicks: the packet they would send is handed to the Vulkan renderer instead of
the DMA controller, captured first as reference data, then interpreted (GS primitives, then the VU1
microprograms the VIF1 packets run). `docs/PS2_SERVICES.md` and `docs/BOOT_PATH.md` give the rest of the
shim surface and the order in which boot needs it.
