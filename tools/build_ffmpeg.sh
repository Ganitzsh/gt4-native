#!/bin/bash
# Builds the static 32-bit FFmpeg the native game links for its movies (runtime/native/movie.cpp) and
# still pictures (runtime/native/ipu_image.cpp): only the MPEG-2 video and IPU decoders, the MPEG-PS
# (PSS) demuxer and parser, the PCM / PS2 ADPCM audio decoders, swscale and swresample. MSYS2's mingw32 repository no longer has an FFmpeg package.
# Needs MSYS2 with the MINGW32 toolchain, make and diffutils. Output: build/ffmpeg-i686 (or $1).
#   C:\msys64\usr\bin\bash.exe tools/build_ffmpeg.sh
set -euo pipefail
version=7.1.1
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${1:-$root/build/ffmpeg-i686}
src=$root/build/ffmpeg-src
export MSYSTEM=MINGW32 PATH=/mingw32/bin:/usr/bin:$PATH
mkdir -p "$src"
cd "$src"
[ -f ffmpeg-$version.tar.xz ] || curl -sL -o ffmpeg-$version.tar.xz https://ffmpeg.org/releases/ffmpeg-$version.tar.xz
[ -d ffmpeg-$version ] || tar xf ffmpeg-$version.tar.xz
cd ffmpeg-$version
./configure --prefix="$prefix" --arch=x86 --target-os=mingw32 --cc=gcc --enable-static --disable-shared \
    --disable-programs --disable-doc --disable-network --disable-autodetect --disable-everything --disable-x86asm \
    --disable-debug --enable-decoder=mpeg2video,mpeg1video,ipu,pcm_s16le,pcm_s16be,adpcm_psx \
    --enable-demuxer=mpegps,mpegvideo --enable-parser=mpegvideo,mpegaudio --enable-protocol=file \
    --enable-swscale --enable-swresample
make -j"$(nproc)"
make install
