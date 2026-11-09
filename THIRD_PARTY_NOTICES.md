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

`gt4_host` (built only with `-DGT4_BUILD_HOST=ON`) uses the components below. The vendored ones live
under `third_party/` with their licence files (provenance in `third_party/README.md`); the others are
fetched by CMake at pinned commits (`cmake/HostDependencies.cmake`) and are not part of this repository.

| Component | Where | Licence | Use |
|---|---|---|---|
| AMD FidelityFX Super Resolution 2.2.1 | `third_party/fidelityfx-fsr2/` (vendored, unmodified) | MIT, Copyright (c) 2022-2023 Advanced Micro Devices, Inc. (`third_party/fidelityfx-fsr2/LICENSE.txt`) | FSR 2 temporal upscaling; its RCAS header for sharpening. |
| AMD FidelityFX Super Resolution 1.0, EASU | `platform/render/shaders/easu.comp` (a GLSL port of `ffx_fsr1.h`) | MIT, Copyright (c) 2021 Advanced Micro Devices, Inc. (same terms as FSR 2 above) | FSR 1 spatial upscaling. |
| SMAA | `third_party/smaa/` (vendored, unmodified) | MIT-style, Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez (`third_party/smaa/LICENSE.txt`) | SMAA 1x / T2x anti-aliasing: the shader and its precomputed area and search textures. |
| stb_image 2.30 | `third_party/stb/` (vendored, unmodified) | Public domain (Unlicense) or MIT, Sean Barrett (the end of `stb_image.h`) | Decodes texture packs' PNG files. |
| stb_truetype 1.26 | `third_party/stb/` (vendored, unmodified) | Public domain (Unlicense) or MIT, Sean Barrett (the end of `stb_truetype.h`) | Reads the bundled UI fonts below and renders their glyphs as signed distance fields; never used on other fonts. |
| Saira, Saira Semi Expanded, Libre Caslon Text | `assets/ui/fonts/` (unmodified), embedded in gt4_host | SIL Open Font License 1.1 (below) | The host UI's text. |
| xxHash 0.8.3 | `third_party/xxhash/` (vendored, unmodified) | BSD-2-Clause, Copyright (c) 2012-2021 Yann Collet (`third_party/xxhash/LICENSE`) | The hashes that name a texture pack's replacements. |
| SDL 3 | fetched, linked | zlib | Window, input, audio, timing. |
| Vulkan-Headers | fetched, headers only | Apache-2.0 OR MIT | Vulkan API declarations. |
| volk | fetched (at Granite's pinned commit), compiled into gt4_host | MIT | Vulkan function loader. |
| glslang | fetched, build tool only (nothing linked) | BSD-3-Clause and others (its LICENSE.txt) | Compiles the GLSL shaders to SPIR-V during the build. |
| paraLLEl-GS | fetched only with the CMake option `GT4_GS_REFERENCE` (https://github.com/Arntzen-Software/parallel-gs, commit `cc6184af`), linked into two test programs, never into gt4_host | LGPL-3.0-or-later, Copyright (c) 2024 Arntzen Software AS (Hans-Kristian Arntzen, Runar Heyer) | A reference for the native renderer's tests: the PS2 Graphics Synthesizer emulated with Vulkan compute shaders (`tests/gs_backend.cpp`, `tests/gs_native.cpp`). |
| Granite | fetched as paraLLEl-GS's submodule (commit `16e7395f`) with `GT4_GS_REFERENCE` only, linked into the same test programs | MIT, Copyright (c) 2017-2026 Hans-Kristian Arntzen | paraLLEl-GS's Vulkan layer (device, memory, command buffers). Only Granite itself is fetched, not its own submodules. |

**paraLLEl-GS and the LGPL.** paraLLEl-GS is licensed under the GNU Lesser General Public License,
version 3 or later. Since 2026-10-30 gt4_host does not contain it: only the reference tests of a
`GT4_GS_REFERENCE` build link it (statically; they are GPL-3.0 programs like the rest of the project, which the
LGPL permits), and they are not distributed. Should such a test binary be distributed, it must carry this
notice, the GPL-3.0 and LGPL-3.0 texts, and paraLLEl-GS's and Granite's copyright notices, with the
corresponding source (`cmake/HostDependencies.cmake` names the revision). paraLLEl-GS is used unmodified.

**PCSX2 (reference only).** The VIF1 and VU1 interpreter of the development scaffold
(`platform/gs/vif1.*`, `platform/gs/vu1.*`, CMake option `GT4_VU1_INTERPRETER`) was written for this
project from the public VU/VIF documentation. For behaviour the public documents leave open (flag and
Q/P pipeline latencies, the integer-branch read of the previous VI value, the same-register write of both
instruction slots, UNPACK filling writes and V2/V3 field order, the double-buffer swap on MSCAL), PCSX2's
interpreter (https://github.com/PCSX2/pcsx2, GPL-3.0-or-later, Copyright (c) 2002-2026 PCSX2 Dev Team) was
consulted as a reference; no PCSX2 source is copied.

**PCSX2 texture names (reference only).** `platform/gsnative/texture_name.cpp` computes a texture's name as
PCSX2 (https://github.com/PCSX2/pcsx2, GPL-3.0-or-later, Copyright (c) 2002-2026 PCSX2 Dev Team) does for its
texture replacements, so that packs made for it apply here. It was written from a description of PCSX2's
`GSTextureReplacements` and `GSTextureCache` hashing; no PCSX2 source is copied.

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

**PS2 float model (LGPL-3.0-or-later).** `runtime/ps2float/ps2float.c` and `.h` are copied unchanged from
libretro/parallel-ps2 (`pcsx2/ps2float.*`, revision 32f5d699cc084f2d197072a24e7c7806121a1339), a bit-exact
integer model of the EE and VU floating-point units (Booth multiplier, SRT divider) by the PCSX2 Dev Team and
contributors. They keep their license header; the LGPL-3.0-or-later permits combining them with this GPL-3.0
program. The game uses them through `gt4_ee_mul` and friends (runtime/sdk.cpp).

## FFmpeg

The native game links a static FFmpeg (LGPL-2.1+, https://ffmpeg.org) for its movies, built by
`tools/build_ffmpeg.sh` with only the MPEG-2 video decoder, the MPEG-PS demuxer, PCM/PS2 ADPCM,
swscale and swresample. Its source is the release tarball that script downloads.

## UI fonts (SIL Open Font License 1.1)

gt4_host embeds these fonts, unmodified, for its user interface (`assets/ui/fonts/`, which also holds each
project's licence file): `SairaSemiExpanded-BoldItalic.ttf`, `SairaSemiExpanded-Regular.ttf` and
`Saira-Regular.ttf` from Saira (https://github.com/Omnibus-Type/Saira), and `LibreCaslonText-Regular.ttf` from
Libre Caslon Text (https://github.com/thundernixon/Libre-Caslon).

Copyright 2020 The Saira Project Authors (https://github.com/Omnibus-Type/Saira)

Copyright 2018 The Libre Caslon Text Project Authors (https://github.com/thundernixon/Libre-Caslon)

```
This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is copied below, and is also available with a FAQ at:
https://scripts.sil.org/OFL


-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded, 
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```

## MIT KEMAR HRTF measurements

Applies to `platform/audio/spatial/hrtf_kemar.h`, a table of head-related impulse responses derived from
MIT's KEMAR measurements (the compact set, horizontal plane, resampled to 48 kHz) by `tools/hrtf_table.py`; the
measurements themselves are not included.

Bill Gardner and Keith Martin, "HRTF Measurements of a KEMAR Dummy-Head Microphone", MIT Media Lab Perceptual
Computing Technical Report #280, 1994. The data is Copyright 1994 by the MIT Media Laboratory: "It is provided free
with no restrictions on use, provided the authors are cited when the data is used in any research or commercial
application." (the archive's README).
