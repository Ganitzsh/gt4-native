#!/usr/bin/env bash
# Optional Ubuntu/WSL build environment without sudo or system package changes.
set -euo pipefail
gt4_native_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gt4_native_deps="${XDG_DATA_HOME:-$HOME/.local/share}/gt4/native-deps"
case "${1:-}" in
  setup)
    mkdir -p "$gt4_native_deps/debs"
    cd "$gt4_native_deps/debs"
    apt-get download libx11-dev libx11-6 libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
      libxfixes-dev libxss-dev libxtst-dev libxrender-dev libxext6 libxrandr2 libxcursor1 \
      libxi6 libxfixes3 libxss1 libxtst6 libxrender1 libpulse-dev libpulse0 libsndfile1 \
      libogg0 libvorbis0a libvorbisenc2 libflac12t64 libmpg123-0t64 libopus0 libasyncns0 libasound2t64
    for gt4_native_package in ./*.deb; do dpkg-deb -x "$gt4_native_package" "$gt4_native_deps"; done
    ;;
  build)
    export PKG_CONFIG_PATH="$gt4_native_deps/usr/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    cmake -S "$gt4_native_root" -B "$gt4_native_root/build/host-linux" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$gt4_native_deps/usr" \
      "-DCMAKE_C_FLAGS=-I$gt4_native_deps/usr/include"
    cmake --build "$gt4_native_root/build/host-linux" --parallel 4
    ;;
  run)
    shift
    export LD_LIBRARY_PATH="$gt4_native_deps/usr/lib/x86_64-linux-gnu:$gt4_native_deps/usr/lib/x86_64-linux-gnu/pulseaudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    exec "$gt4_native_root/build/host-linux/gt4_host" "$@"
    ;;
  *) echo "Usage: bash tools/native_linux.sh setup|build|run [native arguments]" >&2; exit 2 ;;
esac
