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
