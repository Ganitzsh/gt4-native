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
