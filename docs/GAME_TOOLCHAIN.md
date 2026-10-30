# The game toolchain: the decomp's sources as 32-bit Windows objects

The native program maps the original game image (its `.data`, vtables, objects) at the original
addresses, so the decompiled game code must be compiled with the **same data layout and C++ ABI as
the PS2 build**: the gcc 2.x ABI (vptr after the first polymorphic class's fields, 8-byte
`{short delta; short index; pfn}` vtable entries, in-charge destructor argument, gcc 2.x mangling)
and the EE's data model (64-bit `long`). No modern compiler does that, so gcc 2.x itself is
retargeted to i386.

| Piece | Where |
|---|---|
| Compiler build script | `tools/game_toolchain/build_gcc29.sh` |
| Compiler patch (everything changed in gcc) | `tools/game_toolchain/gcc-2.9-ee-990721-i386-gt4.patch` |
| Compile driver (Windows Python) | `tools/game_cc.py` |
| Native compat headers (first on the include path) | `compat/game/` |
| ABI probes and comparator | `tools/game_toolchain/abi_probe/` |

## The compiler

Source: Sony's ee-gcc kit, `~/.local/share/gt4/ee-gcc2.9/src/src.tar.gz` (gcc `2.9-ee-990721`, a
gcc 2.95-era snapshot). Target `i386-pc-mingw32` (PE-COFF, cdecl, leading `_` on C names), host
32-bit Linux (`gcc -m32`, as gcc 2.9's host configuration expects: `HOST_BITS_PER_LONG` 32), in WSL.

```sh
bash tools/game_toolchain/build_gcc29.sh            # ~25 s; installs ~/.local/share/gt4/i386-gcc29
~/.local/share/gt4/i386-gcc29/bin/i386-gt4-gcc -S -O2 -fno-exceptions foo.cpp
```

Only the driver (`bin/i386-gt4-gcc`, `i386-gt4-g++` links to it) and `cc1`, `cc1plus`, `cpp` are
installed (5 MB). No libgcc, headers or C++ library: the game code is freestanding (the EE compiler
had no system include directories either), the objects link against a modern mingw32 libgcc
(`__divdi3`, `__moddi3`, `__cmpdi2`, `__floatdisf`, `__alloca`: all present in MSYS2's
i686 libgcc 16). Assemble its output with MSYS2's `as --32`.

Host build fixes (in the patch or the script): `-std=gnu89 -fcommon -w` and old-C warnings off;
`-D_FILE_OFFSET_BITS=64` (a 32-bit `stat` fails with EOVERFLOW on `/mnt/c`); the kit's own bison
1.25 (modern bison rejects gcc 2.9's grammars); `include/obstack.h` cast-as-lvalue increments
rewritten; `genrtl.h`/`tree-check.h` generated before the parallel build (winnt.o lacks the
dependency); libiberty built from the top-level configure.

### Target patches (`gcc/config/i386/gt4-native.h`, included after mingw32.h and crtdll.h)

| Setting | Stock i386-mingw32 | Patched (= EE) |
|---|---|---|
| `long` / `unsigned long` | 4 bytes | **8 bytes, 8-aligned** (DImode, as `long long`) |
| `long double` | 12 bytes (x87 extended) | **8 bytes** (= double) |
| `bool` (C++) | 1 byte | **4 bytes** (cp/decl.c sizes it by `SLOW_BYTE_ACCESS`, which MIPS sets) |
| `wchar_t` | 2 bytes, unsigned | **4 bytes, signed `int`** |
| `double`, `long long`, `long` in structs | 8-aligned (cygwin.h `BIGGEST_ALIGNMENT` 128) | 8-aligned (kept, stated explicitly) |
| Predefined macros | `_WIN32 WIN32 __MINGW32__ WINNT ...` | none of those (expat/newlib sources would take Win32 paths); `__GT4_NATIVE__`, `i386`, `__i386__`, `__LONG_MAX__=9223372036854775807L` (as ee-gcc's driver), `__GNUC_MINOR__=96` (ee-gcc 2.96's; the SGI STL and libio test it) |
| vtables | non-thunk (gcc 2.9 default `-fno-vtable-thunks`) | same; ee-gcc 2.96 is non-thunk too (verified) |
| Floating point | x87 | x87 (gcc 2.9 has no SSE). The runtime sets the x87 precision control to single for the EE's float behaviour; note that this also rounds `double` arithmetic to single (the EE does doubles in software, the game rarely uses them). |

`int`, `size_t`, `ptrdiff_t`, pointers and enums stay 32-bit; `char` is signed on both.

**Call-site padding (`GT4_CALL_PAD` 32, `calls.c`).** Every call reserves 32 bytes above its pushed
arguments (`addl $-32,%esp` before the pushes; the pop after the call includes them; pending pops are
flushed first so the padding is always directly above the arguments; a const/malloc call's padding
is inside its libcall sequence). The decomp declares many functions with more parameters than their
callers pass, harmless on the EE (parameters are registers) but natively the callee owns those
stack slots and may write them (an assignment to a parameter, or gcc 2.9's sibling calls, which
store the next callee's arguments into the caller's incoming slots: `movl $0,12(%ebp); leave; jmp`).
With the padding, slots up to 8 words past the actual arguments are the caller's scratch. The
callee's view is plain cdecl (`[esp+4]` = first argument), so runtime code calls game code and back
unchanged. gcc 2.9 emits a sibling call only when the callee's arguments fit in the caller's
*declared* incoming area (`current_function_args_size`), so it never writes past it.

**Large aggregates by invisible reference (`gt4-native.h`), as the EE.** ee-gcc's EABI passes an argument
larger than 8 bytes (or of variable size) as the address of the caller's object, and the callee copies a
named one before changing it (`abi64.h` `FUNCTION_ARG_PASS_BY_REFERENCE` / `FUNCTION_ARG_CALLEE_COPIES`,
`mips.c` `function_arg_pass_by_reference`); 16-byte 16-aligned values (TImode, the 128-bit integers) stay by
value. The native compiler does the same. The decomp relies on it: matched sources call the STL heap
instantiations with `&tmp` where the template takes the 12-byte value (`00607758` -> `__push_heap`
`00607A80`, `005FC170`, `005FCC20`, `00608488`), the same call on the EE. Copied by value onto the stack,
the callee took the pointer and the comparator as the element: the vector font's polygon fill sorted
garbage edges and GT mode crashed in its comparator (`006083D0`) when the garbage was unmapped. A struct
passed through `...` is a pointer too (no game source does that).

**No sibling calls (`calls.c`).** `expand_call` expands a call twice when a sibling call is
possible (one insn chain each), and this snapshot's second expansion of the arguments is unsound:
- a C++ class with a copy constructor is passed by value as the address of a copy built in a
  `TARGET_EXPR`'s slot, and the second expansion reused a slot the first had freed as the next
  argument's temporary: of two such arguments, both were the second (`void f(It a, It b) { g(a, b); }`
  called `g(b, b)`). It broke every two-iterator call of the game's STL, e.g. `map::erase(first, last)`
  erased nothing (the unloaded menu project stayed registered and was unloaded again), and the
  widget sort's insertion step (the comparator copy went where the element belongs);
- `UNSAVE_EXPR` reset saved expressions, so a `new` whose value is saved allocated twice (below).
Sibling calls also store into the caller's incoming argument area (see the call-site padding), so
they are now never made.

**A new-expression's value is the allocated pointer (`cp/init.c`).** In gcc 2.x a constructor
returns `this` and `new T(...)` takes its value from the constructor call. The decomp defines many
constructors as plain functions under an asm label (`T(...) __asm__("func_0047FB80")`) that return
nothing; on the EE `$v0` happened to hold `this` when they returned, natively `eax` did not (the
race's script nodes got an owner pointer of 1). The pointer the allocation returned is used instead,
as gcc already did for Java constructors.

### Frontend patches (gcc 2.96 behaviour the decomp relies on)

gcc 2.96 (2000-10-03 snapshot, the game's compiler) has a newer frontend than this 2.9 snapshot.
The patch adds what the census showed the sources need:

- **C: a typedef repeated with the same type** is accepted (`c-decl.c`); sources repeat types.h's.
- **C: unnamed struct/union members** (`struct S { union { int a; float f; }; };`, generated
  headers use them): any struct/union *specifier* without a declarator is an unnamed member, as on
  ee-gcc 2.96, tagged or not (a typedef name is not) (`c-parse.in`); `lookup_field` searches unnamed
  members before its binary search (`c-typeck.c`, large structures missed them).
- **C++: anonymous structs** as members, like anonymous unions (`cp/decl.c`, `class.c`, `search.c`,
  `typeck.c`, `ANON_AGGR_TYPE_P`); the "anonymous class type not used" pedwarn is a warning.
- **Asm labels** (`int f () __asm__ ("func_00330900");`): the symbol gets the `_` user-label
  prefix like any C name (otherwise a function named by label in one source and by C name in another
  would be two symbols); the label also becomes the decl's assembler name, so a deferred inline
  function named by a label is emitted when referenced and its link-once section is named after it
  (`varasm.c`, as ee-gcc 2.96). A **destructor** keeps its mangled assembler name (`_$_A`), by which
  gcc 2.9 recognises destructors, and only its symbol takes the label (`cp/decl2.c`); a deferred
  inline is also emitted when referenced by its label.
- **`-fgt4-extern-class-data`** (default on): vtables (`_vt$...`), type-info functions (`__tf...`)
  and type-info objects (`__ti...`) are never defined by an object, only referenced: the native
  link resolves them to the game image's own (their entries point at the original code addresses;
  `dynamic_cast` compares type-info addresses). `-fno-gt4-extern-class-data` emits them as gcc
  normally does (for the ABI probes).
- **`malloc` declared `void`** (`extern "C" void malloc(s32);`, 4 sources): gcc 2.9 recognises
  malloc/calloc/realloc by name and moved the (absent) return register, an internal compiler error
  (or signal 11). A malloc-named function that returns no pointer is an ordinary call (`calls.c`).
- **`&&label`** is not a jump to the label: gcc 2.9 checked taking a label's address against the
  initializations between it and the label ("jump to label crosses initialization", computed-goto
  interpreters); gcc 2.96 does not (`cp/decl.c` `lookup_label`, `cp/semantics.c`).
- **`extern "C" int f ();` means no arguments** in C++, as in gcc 2.96: the driver passes
  `-fstrict-prototype` to C++ compiles (gcc 2.9 read it as `(...)`, so such a function did not
  convert to `int (*)()`).

Varargs, `__builtin_expect` and the 128-bit types are handled without compiler changes, in
`compat/game/gt4_prelude.h` (below).

## ABI verification against ee-gcc 2.96

Probes in `tools/game_toolchain/abi_probe/` (`run.sh` compiles each with
`~/.local/share/gt4/ee-gcc2.96` `-O2 -G0` and with i386-gt4-gcc; `abi_compare.py` compares every
data label's contents and the global/weak symbol sets). Results:

| Check | ee-gcc 2.96 | i386-gt4-gcc | |
|---|---|---|---|
| sizeof/alignof long, unsigned long, long long, double, long double | 8/8 each | 8/8 each | same |
| sizeof/alignof bool, wchar_t (signed), enum (incl. values > 16 bits), pointer, fn pointer, size_t, ptrdiff_t | 4/4 | 4/4 | same |
| `struct {char; long; char; double; short; long long; float; char; int}` | size 64, offsets 8 16 24 32 40 48 52 56 | identical | same |
| tail padding (`{double; char}`, `{long; int}`), arrays of long, unions, `packed` (13), `aligned(16)` member (offset 16, size 32) | | identical | same |
| bitfields (`unsigned :3 :7 :25`, mixed `char/int:4/short:9/long long:40`, `long:33`, `:0`), bit positions (little-endian, LSB first) | | identical | same |
| vptr placement: `Base {int a; char b; virtual}` | vptr at 8, size 12 | same | same |
| single inheritance chain `Der : Base {long l; int c}` | l@16 c@24 size 32, vptr store at 8 | same | same |
| multiple inheritance `Der2 : Plain, Der` | Der at 8, vptr at 16, z@40, size 48, `_vt$4Der2` deltas -8 | same | same |
| polymorphic class after a non-polymorphic base with a double | q@8, size 16 | same | same |
| virtual base (`VD : virtual VB`) | size 16, `_vt$2VD$2VB` | same | same |
| vtable entries | 8 bytes `{short delta, short index, pfn}`; slot 0 `__tf`, slot 1 destructor, then virtuals in declaration order | same | same |
| vtable size | **one extra all-zero 8-byte entry at the end** | no trailing entry | differs: harmless (indices are the same; with `-fgt4-extern-class-data` the game's own vtables are used) |
| virtual call | `this += entry.delta; call entry.pfn` (`lh` delta, `lw` pfn at slot*8+4) | `movswl` delta, `call *pfn` | same |
| pointer to member function / data | 8 bytes `{delta, index, pfn}` / 4 bytes | same | same |
| destructor in-charge: `delete p` (virtual) / stack object / base from derived | 3 / 2 / passed through | 3 / 2 / passed through | same |
| destructor in-charge for each element of `delete[]` | **2** | **0** | differs (gcc 2.9 frontend): matters only for classes with virtual bases |
| array-new cookie | 8 bytes | 8 bytes | same |
| mangling (124 global/weak symbols of abi.cpp: methods, ctors/dtors `__3Der` `_$_3Der`, `_vt$`, `__tf`/`__ti`, templates `t3Box1Zl`, static members `_t3Box1Zl$n`, namespaces `Q22NS2In`, `wchar_t` `w`, `long double` `r`, varargs `e`, ptr-to-member `M4Base`, `extern "C"`) | | identical (plus the `_` prefix) | same |
| STL (`vector<int, GameAlloc<int>>`, `map<int,long,...>` from the decomp's include/stl) | sizes 16/16; 52 instantiation symbols | identical names and sizes | same; 2.9 inlines less (out-of-line `_$_t9GameAlloc1Zi` etc.) and does not emit the unused `_S_instanceless` statics |
| link-once | `.gnu.linkonce.*` + `.weak` | `.text$name` / `.data$name` + `.linkonce discard` | equivalent |
| C names | `name` | `_name` (PE `USER_LABEL_PREFIX`) | by design |
| asm labels | `func_X` | `_func_X` | consistent with C names (patched) |
| function-local statics | `local.75` | `local.77` | local labels only |

Run-time check of the 64-bit `long` (`longops.c` compiled by i386-gt4-gcc, `longops_main.c` by
modern mingw gcc with `long long`, linked together): add, sub, mul, signed/unsigned div and mod,
shifts by variables and constants, signed/unsigned compares, conversions to/from int, double and
float, varargs: **3016 checks, 0 failures**.

Notes for code that calls into game code (the native runtime, built by modern gcc):
the game's `long` is `long long` there (mingw's `long` is 32-bit); 64-bit arguments take two stack
slots; structs are returned as the EE returns them (build_gcc29.sh patches `RETURN_IN_MEMORY`):
1-, 2- and 4-byte structs in `eax`, 8-aligned 8-byte structs in `edx:eax`, everything else through a
hidden first-argument pointer that the caller pops (so an 8-byte struct of two ints goes through
memory here but in registers under modern mingw); C++ exceptions differ (below).

The EE returns any struct with a scalar mode in `$v0`, so the decomp declares one function both
ways (a one-pointer iterator by value in one source, a plain `int` in another) and both match;
with the EE's rule the native calls agree too. `-fpcc-struct-return` (all structs through memory)
broke those pairs.

Calls through a pointer to member function always include the virtual case (index > 0: the
function is in the vtable at `this + delta2`), as gcc 2.96 does; 2.9 dropped it when the class
has no virtual functions, and the decomp often declares the class as a plain struct (the script
engine's operators: `mUnaryOperator::execute` jumped to the PMF's `delta2` word, 4).

## The driver: `tools/game_cc.py`

```
py tools/game_cc.py --decomp C:/Users/julie/gt4-decomp --out build/game --all [--jobs 8]
py tools/game_cc.py --decomp ... --out build/x src/race/RacePause/RacePause__render.c ...
py tools/game_cc.py --decomp ... --out build/x --list files.txt [--keep-s] [--export own|all]
```

Runs on Windows. Per source: the profile from its first line (`/* compiler: NAME */`, else the
project compiler), read from the decomp's `project.toml`; EE flags mapped to i386 ones (`-O2`,
`-fexceptions`/`-fno-exceptions`, `-fno-implicit-templates` and the STL include dirs for the stl
profile, `-fno-gcse`, `-fno-schedule-insns` kept; `-G0`, `-m*`, `-B`, `-g`, `-c` and
`-fno-optimize-sibling-calls` (2.96 only) dropped); `-fno-strict-aliasing` always; include dirs
`-Icompat` (compat/game) first, then the profile's; `-include compat/gt4_prelude.h`.
Sources go to WSL in batches of 1500 as one tar on stdin per `wsl` call, compile there in parallel
(`--jobs`, each compile killed after `--timeout` 60 s: recorded as a failure), and come back as a tar
of `.s`; Windows' MSYS2 `as --32` assembles them (`--jobs` threads). A full run over 30,257 sources
takes about 2.5 minutes. The decomp is never written; `--out` must be outside it.

Outputs: `obj/<src path>.o`; `s/<src path>.s.err` (failed compiles) and `s/<src path>.s` (failed
assemblies; all with `--keep-s`); `census.jsonl` (`file, profile, lang, ok, stage
(compile|timeout|assemble), first_error, category, compile_seconds, exports`);
`census_summary.txt`.

### On-the-fly source transforms (`transform()`)

- **128-bit types**: every `typedef [unsigned] int NAME __attribute__((mode(TI)));` becomes
  `typedef struct __gt4_u128 NAME;` (`__gt4_s128` for signed): 16-byte, 16-aligned structures
  (`gt4_prelude.h`; `compat/game/types.h` uses them for `u128`/`s128`). The 32-bit-host compiler has
  no TImode integer type. Copies work; arithmetic on them does not compile.
- **Hexadecimal float constants** (`0x1.8p3f`, C99; 2.96 reads them, 2.9 rejects them in C++ and
  crashes on them in C) become decimal constants of the same value (shortest round-trip digits).
- **One-instruction MIPS asm with a C meaning** become C: `min.s`/`max.s`/`sqrt.s`/`abs.s`/
  `neg.s`/`rsqrt.s` (x87), `mtc1`/`mfc1` (bit moves), `plzcw`; `sync*`, `pref`, `cache`, `vnop`
  become a compiler barrier (`asm volatile ("" ::: "memory")`); `li %0, IMM` a constant; the EE
  kernel's privileged instructions become calls into the native runtime, declared in
  `gt4_prelude.h`: `__gt4_ee_ei()`, `__gt4_ee_di()`, `__gt4_ee_mfc0(reg)`, `__gt4_ee_get_gp()`,
  `__gt4_ee_set_gp(gp)`, `__gt4_ee_break(code)`. Native sources written without asm (`game_src/`)
  use their plain-C forms instead: `gt4_interrupts_enabled()` (Status.EIE as the game reads it),
  `gt4_in_interrupt()`, `gt4_interrupts_disable()`/`gt4_interrupts_enable()`, `gt4_cycle_count()`
  (Count), `gt4_get_gp()`/`gt4_set_gp()`, and `gt4_status_ie()` (Status.IE, always 1 natively).
- **Every other asm statement** (several instructions, MMI, COP2/VU0 macro mode, explicit
  registers, asm in a single-line `#define`) goes to `tools/game_toolchain/asm_translate.py`, which
  turns it into C when it knows all its instructions (else it is left alone and the compile fails as
  before):
  `({ temporaries; inputs copied in; one helper call per instruction; outputs copied out; })`.
  Register operands are `__gt4_reg` temporaries (the EE's 128-bit GPR; a 32-bit value
  sign-extended into it, non-MMI instructions write the low 64 bits, MMI all 128); `"f"` operands
  are floats; `"m"` operands are used by address; an input tied to an output (`"0"`) initialises it.
  Explicit registers (`$8`, `$v0`, `$f2`, HI/LO of the MMI multiplies, the FPU accumulator) are
  file-scope statics, so a value goes from one asm statement to the next as in the original;
  `$28`/`$gp` goes through `__gt4_ee_get_gp`/`set_gp`. Because the EE keeps 128 bits in a register
  even when the C variable bound to it is smaller (`qmfc2 %0,$vf1 : "=r"(u64 lo)` then
  `pcpyud %0,%1,%1 : "=r"(hi) : "r"(lo)` reads lo's high half), a plain variable that is a register
  output and a register input in the same file gets a shadow static of its full register, used
  while its low bytes still equal the variable. VU0 registers are one program-wide register file,
  `__gt4_vu0` (`runtime/native/vu0.cpp`); `vcallms` runs GT4's VU0 programs in C++ (`runtime/native/vu0_programs.cpp`).
  The semantics (MMI lanes and saturation, FPU incl. the EE's truncating `cvt.w.s`, VU0 FMAC with
  dest masks and broadcasts, Q/I/R, clipping, VU0 integer and memory ops, COP2 moves, COP0 and
  `syscall` as runtime calls) are C helpers in `compat/game/gt4_asm.h`, included by the prelude.
  VU0 arithmetic, in macro and micro mode, uses the VU's number format (`__gt4_vu_float`): no
  infinities, NaNs or denormals. The game relies on it: 00499948 places text glyphs from vectors
  whose z holds a coordinate's sign-extended upper word (0xFFFFFFFF for a negative x), multiplied by
  zero; as an IEEE NaN it spread, and each label lost the letters left of its origin.
  A `#param` stringified into a VU register name in a macro (`"$vf" #n`) becomes the C index `n`.
- **C99 variadic macros** (`#define F(a, ...) ... __VA_ARGS__`) take gcc 2.9's named form.
- **Register variables** (`register int i __asm__("$25")`) lose the binding; a file-scope one bound
  to `$28` becomes a macro reading the module's `$gp` from the runtime.
- **`(u128) 0`** (a mode(TI) typedef name) becomes `__gt4_zero_u128 ()`; C++ also gets integer
  assignment to the 128-bit structures (sign-extended) and copies to/from `volatile` ones (member
  templates and non-copy `operator=`s, so the structures stay trivial for unions).
- **Explicit instantiation of an operator through a typedef** (`template Vec &Vec::operator=(const
  Vec &);`) is preceded by `typedef char __gt4_complete_N[sizeof (Vec)];`: gcc 2.9 needs the class
  complete there.

`gt4_prelude.h` also predefines `__MIPSEL__`, as ee-gcc does (newlib picks its IEEE word order by it).

**Recovered sources** (`--recovered`, implied by `--all`): `config/asm_functions.txt` lists functions
the decomp keeps as assembly although they were matched in C once (`... (was NAME.cpp)`: moved out
by its inline-asm policy). The driver reads their last matched version from the decomp's git
history (`git show`, read only) and compiles them as `recovered/<src path>` (objects under
`obj/recovered/`), exporting the listed address: 39 functions, mostly the VU0 graphics helpers.

`compat/game/` also replaces `spr.h`: `spr_base()` returns `(void *)0x70002000`, assuming the native
program maps the scratchpad at its guest address like the rest of the EE address space.
`gt4_prelude.h` supplies gcc 2.96's varargs builtins over the i386 stack (`__builtin_va_list`,
`__builtin_stdarg_start`, `__builtin_va_arg` with 4-byte slots, ...) and `__builtin_expect`.

### Symbols: what each object exports (`--export own`, the default)

Sources repeat their neighbours' definitions so that each compiles alone (e.g. 12 sources define
`func_00330900`). As the decomp's build does (`objcopy -G STEM* -G func_ADDR*`), every global the
compiler emits that is not the source's own (its file name or `func_<its address>` prefix) becomes
local (`.globl` dropped, COFF `.scl 2` turned to 3). The address of each source comes from the
decomp's own `tools/project.py` (run read-only).

Every `.linkonce` directive is dropped too. gcc puts inline functions and template instances in
COMDAT sections named after the function (`.text$_$_6String`), and the PE linker keeps one section
per name program-wide. With the symbols local, each object's calls go to its own copy; when the
linker dropped that copy for another object's, the call was left pointing at the next instruction
(969 calls in 2026-10-25's image, in `String` destructors, `assign` and map inserts). Sources do not
even agree on these helpers: each declares its own `String`.

Each object also exports **`_gt4fn_<ADDR>`** (uppercase hex), an alias of the function at the
source's address: the function named after the source if there is one, else the first function of
the primary `.text` (the decomp's build places that section at the address). A source that only
instantiates a template has its function in a `.text$...` section (no longer link-once). gcc 2.9
inlines less than the EE compiler, so the helpers the EE code had inlined (allocator, base-class
destructors) come out next to it, sometimes first: the alias names the `.text$` function nothing
else in the object calls (the instance itself), the largest if several. Taking the first one
pointed `vector<HSymID>`'s copy constructor (0030F740) at `GameAlloc`'s destructor.
All 30,189 objects have their alias; a symbol scan finds no duplicate global definition.

## Census

**2026-10-25, second run: all 30,257 sources and the 39 recovered ones compile and assemble
(30,296 of 30,296).** What fixed the 68 below: the compiler (malloc ICE: the 3 signal 11 and the
boot-path internal error; `&&label`), `-fstrict-prototype` (the 2 `extern "C" f()` conversions),
the general asm translation (30 MIPS asm, 17 `emit_move_insn` crashes), the transforms (C99 variadic
macros, register variables, `__as` instantiations, `volatile u128`), `__MIPSEL__` (newlib dtoa).
Compiling a 2,467-source sample (all u128 users, the newlib units, 1,800 random others) before and
after the prelude and flag changes gave identical code up to local label numbers.

First run, 2026-10-25, decomp at that day's master, 30,257 sources:

**30,189 compile and assemble (99.8%)**: 23,848 of 23,905 C++ files, 6,341 of 6,352 C files.

| Category | Files | Examples |
|---|---|---|
| MIPS inline asm | 30 | VU0: `lqc2 $vf15, ...`, `qmfc2`, `vcallms` (network/unit_00494578, VU_GET_MATRIX); `mtc0 $0,$25`; `"=f"` outputs of multi-instruction or VU asm (engine/unit_00457510/func_00457918.cpp: "Output constraint 0 must specify a single register"); `$28`/`$gp` register names in clobbers or register variables (libstream/unit_0057A128/func_00586208.c) |
| compiler crash | 21 | 17 × `emit_move_insn` internal error: MMI asm with a 128-bit operand in a register (`pcpyld %0 ... "=r"(u128)`, engine/unit_00450248/func_00451238.cpp, VU_SET_VF); 3 × cc1 signal 11 (runtime/hArrayCompare/func_005C99C0.c); 1 internal error (startup/unit_00101400/func_00108BF8.cpp) |
| frontend (2.96-only constructs) | 13 | `jump to label 'next'` across an initialisation (adhoc/unit_00321330/hThread__execute0.cpp); explicit `__as` member declaration (runtime/unit_005C1CE0/func_005C5790.cpp, 5 files); `sorry, not implemented: tree_list ... dump_type`; `no matching function for call to Callback::Callback (void (&)(...), int)`; `cannot convert to a pointer type`; `badly punctuated parameter list in #define` (2); newlib dtoa's `Sign_bit`/`Exp_mask` undeclared (2: its endianness macros key on `__MIPSEL__`) |
| 128-bit/MMI types | 4 | `volatile u128` copies (`conversion from volatile __gt4_u128 discards qualifiers`) |
| missing headers, timeouts, assembler errors in compiler output | 0 | |

The 2.9 frontend's rejections of 2.96-only syntax were far below the 5% threshold even before the
fixes: in the first run 768 sources (2.5%) failed, about 230 of them (0.8%) on 2.96-only constructs,
mostly the C unnamed members and asm-labelled destructors since patched. gcc 2.96 sources were
therefore not fetched. History of the first runs:
97.5% (unpatched frontend; 498 MIPS asm, 123 C unnamed members, 46 asm-labelled destructors, 25
C++ anonymous structs), 99.5% (asm translation, anonymous members), 99.7%, 99.8%.

## What blocks linking the objects into one program

Symbol scan of all 30,189 objects (`nm`): no duplicate global definitions; the undefined names are:

| Undefined | Names | What resolves them |
|---|---|---|
| game data `D_<ADDR>` | 25,031 | the game image at the original addresses |
| game functions `func_<ADDR>` | 3,247 (1,060 of them C++-mangled, `_func_00100BB8__Fi`: declared without `extern "C"`) | native objects (`_gt4fn_ADDR`) or trampolines to original code; the mangled ones need their `__F...` suffix stripped |
| other named game symbols | 1,399 (`AutomaticFader__update`, C++-mangled methods, `___13func_00101030` constructors) | the decomp's symbol table (`config/symbol_addrs.txt`) |
| vtables / type info | 158 `_vt$...` (and `__tf`/`__ti` where used) | the game image's (`-fgt4-extern-class-data`) |
| EE kernel hooks | 6 `__gt4_ee_*` | the native runtime |
| gcc 2.x C++ runtime | `__builtin_new`, `__builtin_delete`, `__builtin_vec_new`, `__builtin_vec_delete`, `__rtti_user`, `__rtti_si`, `__rtti_class`, `__dynamic_cast`, `__builtin_type_info__vtable` and the other `*_type_info__vtable`, `__eq__C9type_infoRC9type_info`, `__tfv` | the game's own copies (src/runtime, compiled here too) or the image |
| **exception handling** | `__sjthrow`, `__start_cp_handler`, `__get_eh_context`, `__eh_alloc`, `__cp_push_exception`, `__cp_pop_exception`, `__check_eh_spec`, `__uncatch_exception`, `__throw_type_match_rtti_2`, `__terminate`, `__builtin_init_dwarf_reg_size_table` | **blocker for the 23 `-fexceptions` sources**: gcc 2.9's i386-mingw32 uses setjmp/longjmp exceptions, while the EE build (and the game's own libgcc runtime in src/runtime) uses DWARF2 unwind tables. Native C++ exceptions need gcc 2.95's sjlj runtime (libgcc2.c `__sjthrow`, cp/exception.cc) compiled with this compiler, or those paths kept out of native code. `__builtin_init_dwarf_reg_size_table` is a 2.96 builtin the 2.9 frontend treats as an external function (in the game's frame-unwind code). |
| libgcc | `__divdi3`, `__moddi3`, `__udivdi3`, `__umoddi3`, `__cmpdi2`, `__floatdisf`, `__alloca` (stack probe of frames over 4 KB) | modern mingw32 libgcc (all present) |

Other things to keep in mind when linking and running:

- All sources compile now (above); hand-written assembly the boot reaches gets native code in `runtime/native/` (one file per decomp unit), VU0 microprograms are C++ in `runtime/native/vu0_programs.cpp`.
- 2.9 inlines less than 2.96; with `--export own` every out-of-line copy is local, so nothing
  collides, but the code is larger.
- `delete[]` passes in-charge 0 instead of 2 to element destructors (classes with virtual bases
  would not have their virtual bases destroyed).
- The EE's float arithmetic is single precision; x87 computes in extended precision unless the
  runtime sets the precision control (and even then exponent range and denormals differ from the
  EE's non-IEEE FPU).
- `spr_base()` assumes the scratchpad is mapped at 0x70002000.
