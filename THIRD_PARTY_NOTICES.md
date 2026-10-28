# Third-party notices

The repository contains a small, explicitly attributed subset of source from `gustavocanepa/gt4-decomp` under `upstream/gt4/`. Each copied source retains its upstream path and commit in a file comment. The upstream repository's MIT license applies to these files unless an individual file says otherwise; see its [license](https://github.com/gustavocanepa/gt4-decomp/blob/ba5ebe778a786d92b108926b8f89843ea1a9b5cf/LICENSE) and [third-party inventory](https://github.com/gustavocanepa/gt4-decomp/blob/ba5ebe778a786d92b108926b8f89843ea1a9b5cf/THIRD_PARTY.md).

The currently selected source subset is pinned to `ba5ebe778a786d92b108926b8f89843ea1a9b5cf`. Its original hashes are in `upstream/gt4/integration.json`. Thirteen function bodies are adapted reproducibly for checked guest-memory access; the generated file retains provenance and records the single explicit caller-argument change in `compat/abi/object_fields_sources.json`. Bulk inventory artifacts are temporary and are not committed. Future imports must preserve per-file notices and applicable licenses. This repository's original work is licensed under GPL-3.0 (see LICENSE), except that sources under `upstream/gt4/` keep the MIT license below. Revisions up to and including commit `5799594b1fa25218f1d1c0b87602036d508aaf53` were published under the MIT license. No game assets, executable, BIOS, or proprietary SDK files are distributed here.

## gt4-decomp (MIT)

Applies to the files under `upstream/gt4/` and to the adapted functions in `compat/abi/`, which are derived from them.

MIT License

Copyright (c) 2026 Gustavo Canepa and gt4-decomp contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## gt4_host: vendored and fetched components

`gt4_host` (built only with `-DGT4_BUILD_HOST=ON`) uses the components below. The two vendored ones live
under `third_party/` with their licence files (provenance in `third_party/README.md`); the others are
fetched by CMake at pinned commits (`cmake/HostDependencies.cmake`) and are not part of this repository.

| Component | Where | Licence | Use |
|---|---|---|---|
| AMD FidelityFX Super Resolution 2.2.1 | `third_party/fidelityfx-fsr2/` (vendored, unmodified) | MIT, Copyright (c) 2022-2023 Advanced Micro Devices, Inc. (`third_party/fidelityfx-fsr2/LICENSE.txt`) | FSR 2 temporal upscaling; its RCAS header for sharpening. |
| AMD FidelityFX Super Resolution 1.0, EASU | `platform/render/shaders/easu.comp` (a GLSL port of `ffx_fsr1.h`) | MIT, Copyright (c) 2021 Advanced Micro Devices, Inc. (same terms as FSR 2 above) | FSR 1 spatial upscaling. |
| SMAA | `third_party/smaa/` (vendored, unmodified) | MIT-style, Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez (`third_party/smaa/LICENSE.txt`) | SMAA 1x / T2x anti-aliasing: the shader and its precomputed area and search textures. |
| SDL 3 | fetched, linked | zlib | Window, input, audio, timing. |
| Vulkan-Headers | fetched, headers only | Apache-2.0 OR MIT | Vulkan API declarations. |
| volk | fetched (at Granite's pinned commit), compiled into gt4_host | MIT | Vulkan function loader, shared by the host and the GS back end. |
| glslang | fetched, build tool only (nothing linked) | BSD-3-Clause and others (its LICENSE.txt) | Compiles the GLSL shaders to SPIR-V during the build. |
| paraLLEl-GS | fetched (https://github.com/Arntzen-Software/parallel-gs, commit `cc6184af`), linked | LGPL-3.0-or-later, Copyright (c) 2024 Arntzen Software AS (Hans-Kristian Arntzen, Runar Heyer) | The GS back end: the PS2 Graphics Synthesizer emulated with Vulkan compute shaders (docs/GS_BACKEND.md). |
| Granite | fetched as paraLLEl-GS's submodule (commit `16e7395f`), linked | MIT, Copyright (c) 2017-2026 Hans-Kristian Arntzen | paraLLEl-GS's Vulkan layer (device, memory, command buffers). Only Granite itself is fetched, not its own submodules. |

**paraLLEl-GS and the LGPL.** paraLLEl-GS is licensed under the GNU Lesser General Public License,
version 3 or later. gt4_host links it statically and is itself distributed under the GPL-3.0 (LICENSE),
which the LGPL permits (an LGPL-3.0 library may be combined into a GPL-3.0 program): the complete
corresponding source of gt4_host, including the paraLLEl-GS revision it is built from
(`cmake/HostDependencies.cmake`), is available under the GPL-3.0, so a recipient can modify paraLLEl-GS and
relink. Binary distributions of gt4_host must carry this notice, the GPL-3.0 and LGPL-3.0 texts, and
paraLLEl-GS's and Granite's copyright notices. paraLLEl-GS is used unmodified.

**PCSX2 (reference only).** The VIF1 and VU1 interpreter of the development scaffold
(`platform/gs/vif1.*`, `platform/gs/vu1.*`, CMake option `GT4_VU1_INTERPRETER`) was written for this
project from the public VU/VIF documentation. For behaviour the public documents leave open (flag and
Q/P pipeline latencies, the integer-branch read of the previous VI value, the same-register write of both
instruction slots, UNPACK filling writes and V2/V3 field order, the double-buffer swap on MSCAL), PCSX2's
interpreter (https://github.com/PCSX2/pcsx2, GPL-3.0-or-later, Copyright (c) 2002-2026 PCSX2 Dev Team) was
consulted as a reference; no PCSX2 source is copied.

**ps2-web-recomp (MIT).** `platform/gs/vu1_ref.*`, a second VU1 interpreter used only to check
`vu1.cpp` (`GT4_VU1_DIFF`, `GT4_VU1=ref`), is adapted from the VU micro interpreter of ps2-web-recomp
(`runtime/src/vu_micro.cpp`, `include/ps2/fpu.hpp`, `vu.hpp`, `vu_ops.hpp`), as vendored by gt4recomp.
`third_party/ps2-web-recomp/` holds its translator (`recomp/`) and the runtime headers the translated code
includes, unmodified (commit `1b480879`), for `tools/eeref`, a test tool (docs/DIFFERENTIAL_TESTING.md).
Copyright (c) 2026 Samir Nuri. Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
do so, subject to the following conditions: The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## FFmpeg

The native game links a static FFmpeg (LGPL-2.1+, https://ffmpeg.org) for its movies, built by
`tools/build_ffmpeg.sh` with only the MPEG-2 video decoder, the MPEG-PS demuxer, PCM/PS2 ADPCM,
swscale and swresample. Its source is the release tarball that script downloads.
