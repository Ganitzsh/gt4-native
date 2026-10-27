#!/bin/bash
# Build the game compiler: Sony's ee-gcc source (gcc 2.9-ee-990721) retargeted to i386-pc-mingw32
# with the PS2 EE's data model (gcc-2.9-ee-990721-i386-gt4.patch, docs/GAME_TOOLCHAIN.md).
# Run in WSL (Linux). Needs gcc-multilib (the compiler is a 32-bit Linux program, as gcc 2.9's
# host configuration expects), make, perl, and the ee-gcc 2.9 kit (its src.tar.gz and its bison
# 1.25: modern bison rejects gcc 2.9's grammars).
#
#   bash tools/game_toolchain/build_gcc29.sh [PREFIX]
#
# Installs PREFIX/bin/i386-gt4-gcc (the driver; i386-gt4-g++ links to it) and cc1, cc1plus, cpp
# under PREFIX/lib/gcc-lib/i386-pc-mingw32/2.9-ee-990721/; no libgcc, headers or libraries (the game
# code is freestanding; link with a modern mingw32 libgcc). The build directory is removed.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX=${1:-$HOME/.local/share/gt4/i386-gcc29}
KIT=${EE_GCC29_KIT:-$HOME/.local/share/gt4/ee-gcc2.9}
B=$(mktemp -d "${TMPDIR:-/tmp}/gt4-gcc29.XXXXXX")
trap 'rm -rf "$B"' EXIT

cd "$B"
keep='src/gcc src/include src/libiberty src/config src/config.sub src/config.guess src/config.if
      src/configure src/configure.in src/Makefile.in src/install-sh src/mkinstalldirs src/move-if-change
      src/symlink-tree src/missing src/ylwrap src/config-ml.in src/ltconfig src/ltmain.sh src/texinfo'
tar xzf "$KIT/src/src.tar.gz" $keep
(cd src && patch -s -p1 < "$HERE/gcc-2.9-ee-990721-i386-gt4.patch")
# The caller, not the callee, pops a function's hidden struct-return pointer: the decomp declares
# the same function both ways (struct by value, or an explicit first pointer argument, which the
# EE treats alike), and a callee's `ret $4` against an explicit-pointer caller shifts %esp by 4.
python3 - src/gcc/config/i386/i386.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "  /* Lose any fake structure return argument.  */\n  if (aggregate_value_p (TREE_TYPE (funtype)))\n    return GET_MODE_SIZE (Pmode);\n"
assert old in s, "i386_return_pops_args changed"
open(p, "w").write(s.replace(old, "  /* gt4: the caller pops the struct-return pointer (see build_gcc29.sh).  */\n"))
PYEOF
# A pseudo is never given a memory equivalence of another mode. combine turns a parameter's
# (set (reg:SI) (mem:SI slot)) into (set (reg:DI) (zero_extend:DI (mem:SI slot))) when the value is
# only used widened, and keeps the REG_EQUIV note on the 4-byte slot; a pseudo that then gets no
# hard register lives there in DImode and its 8-byte store overwrites the next argument (004A81A8:
# the colour correction's green and blue were zeroed by the widened red).
python3 - src/gcc/reload1.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\t  rtx note = find_reg_note (insn, REG_EQUIV, NULL_RTX);\n"
assert old in s, "reload1.c REG_EQUIV scan changed"
new = old + ("\t  /* gt4: a memory equivalence of another mode is not the pseudo's home.  */\n"
             "\t  if (note && GET_CODE (XEXP (note, 0)) == MEM\n"
             "\t      && GET_MODE (XEXP (note, 0)) != GET_MODE (SET_DEST (set)))\n"
             "\t    note = 0;\n")
open(p, "w").write(s.replace(old, new))
PYEOF
# Structs come back in registers exactly when the EE returns them in $v0 (mips.h: any type with a
# scalar mode; MIPS's strict alignment gives a struct DImode only when it is 8-aligned): 1, 2 and 4
# bytes in %eax, 8-aligned 8 bytes in %edx:%eax, everything else through memory. The decomp
# declares one function both ways (a one-pointer iterator by value, or a plain int), which agree
# on the EE and must agree here.
python3 - src/gcc/config/i386/i386.h <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#define RETURN_IN_MEMORY(TYPE) \\\n  ((TYPE_MODE (TYPE) == BLKmode) || int_size_in_bytes (TYPE) > 12)\n"
assert old in s, "i386.h RETURN_IN_MEMORY changed"
new = ("#define RETURN_IN_MEMORY(TYPE) \\\n  ((TYPE_MODE (TYPE) == BLKmode) || int_size_in_bytes (TYPE) > 12 \\\n"
       "   || (AGGREGATE_TYPE_P (TYPE) && (int_size_in_bytes (TYPE) > 8 \\\n"
       "       || TYPE_ALIGN (TYPE) < GET_MODE_ALIGNMENT (TYPE_MODE (TYPE)))))\n")
open(p, "w").write(s.replace(old, new))
PYEOF
# A call through a pointer to member function always handles the virtual case (index > 0: the
# function is in the vtable found at this + delta2), as gcc 2.96 does. 2.9 left it out when the
# class has no virtual functions, and the decomp declares the script engine's operator classes as
# plain structs (mUnaryOperator::execute calls `(Thing::*pmf)` on a `struct Thing { int x; }`).
python3 - src/gcc/cp/typeck.c <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "      if (TYPE_SIZE (basetype) != NULL_TREE\n\t  && ! TYPE_VIRTUAL_P (basetype))\n"
assert old in s, "typeck.c get_member_function_from_ptrfunc changed"
open(p, "w").write(s.replace(old, "      if (0) /* gt4: always handle virtual members (build_gcc29.sh) */\n"))
PYEOF

export CC="gcc -m32"
# gcc 2.9 is K&R/C89 code: old defaults for a modern host compiler; large-file stat for /mnt/c.
export CFLAGS="-O1 -g0 -D_FILE_OFFSET_BITS=64 -std=gnu89 -fcommon -w -fno-strict-aliasing \
  -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion \
  -Wno-incompatible-pointer-types -Wno-return-mismatch"
export BISON_SIMPLE=$KIT/share/bison.simple BISON_HAIRY=$KIT/share/bison.hairy
BISON=$KIT/bin/bison

mkdir obj && cd obj
../src/configure --host=i686-pc-linux-gnu --build=i686-pc-linux-gnu --target=i386-pc-mingw32 \
  --prefix="$PREFIX" --enable-languages=c,c++ --disable-nls > configure.log 2>&1 \
  || { tail -30 configure.log; exit 1; }
make all-libiberty CFLAGS="$CFLAGS" > libiberty.log 2>&1 || { tail -30 libiberty.log; exit 1; }
cd gcc
# Generated headers first: the PE target's extra object (winnt.o) does not depend on them.
make genrtl.h tree-check.h CFLAGS="$CFLAGS" BISON="$BISON" > gen.log 2>&1 || { tail -30 gen.log; exit 1; }
make -j"$(nproc)" LANGUAGES="c c++" CFLAGS="$CFLAGS" BISON="$BISON" cc1 cc1plus cpp xgcc > make.log 2>&1 \
  || { tail -40 make.log; exit 1; }

V=$(sed -n 's/.*"\(.*\)".*/\1/p' ../../src/gcc/version.c)
L=$PREFIX/lib/gcc-lib/i386-pc-mingw32/$V
rm -rf "$PREFIX" && mkdir -p "$PREFIX/bin" "$L"
cp cc1 cc1plus cpp "$L/" && strip "$L"/*
cp xgcc "$PREFIX/bin/i386-gt4-gcc" && strip "$PREFIX/bin/i386-gt4-gcc"
ln -sf i386-gt4-gcc "$PREFIX/bin/i386-gt4-g++"
echo "installed $PREFIX/bin/i386-gt4-gcc"
