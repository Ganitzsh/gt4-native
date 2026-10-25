#!/bin/bash
# Compile ABI probes with ee-gcc 2.96 (the game's compiler) and i386-gt4-gcc, for abi_compare.py:
#
#   bash tools/game_toolchain/abi_probe/run.sh PROBE...        (in WSL; writes PROBE.ee.s, PROBE.i386.s)
#   py tools/game_toolchain/abi_probe/abi_compare.py PROBE.ee.s PROBE.i386.s [-v]
#
# EXTRA adds flags to both (stl.cpp: EXTRA="-Iinc/stl -Iinc/shim -Iinc", the decomp's include/ is
# copied to inc/). Vtables: pass I386_EXTRA=-fno-gt4-extern-class-data to have them defined.
# longops.c + longops_main.c: a run-time test of 64-bit long arithmetic (see GAME_TOOLCHAIN.md).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
D=${EE_GCC:-$HOME/.local/share/gt4/ee-gcc2.96}
G=${GT4_GAME_GCC:-$HOME/.local/share/gt4/i386-gcc29/bin/i386-gt4-gcc}
DECOMP=${DECOMP:-/mnt/c/Users/julie/gt4-decomp}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
# Both compilers are 32-bit programs: work on the Linux side (stat on /mnt/c can overflow).
cp -r "$DECOMP/include" "$W/inc"; cp -r "$HERE/../../../compat/game" "$W/compat"
for f in "$@"; do
  b=$(basename "$f"); cp "$f" "$W/$b"
  ( cd "$W" && "$D/bin/ee-gcc" -B "$D/bin/ee-" -S -O2 -G0 -fno-exceptions -fno-strict-aliasing ${EXTRA:-} \
      -o "$b.ee.s" "$b" ) 2>&1 | sed "s/^/EE: /"
  ( cd "$W" && "$G" -S -O2 -fno-exceptions -fno-strict-aliasing -Icompat -include compat/gt4_prelude.h \
      ${EXTRA:-} ${I386_EXTRA:-} -o "$b.i386.s" "$b" ) 2>&1 | sed "s/^/I386: /"
  cp "$W/$b".*.s "$(dirname "$f")/" 2>/dev/null
done
