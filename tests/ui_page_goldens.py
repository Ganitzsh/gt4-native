#!/usr/bin/env python3
"""The settings hub drawn by the host (MENU_DESIGN.md M3), against committed goldens.

gt4_host --headless draws GT4_UI_PAGE_TEST=hub (the hub laid out from the host's own schema, encoded as a GT4_REC_UI
record and decoded back) over a plain backdrop at 720p, 1440p and 4K, and each capture is compared with
tests/goldens/ui/hub_<height>p.png (our own pixels: the UI fonts are OFL). The backdrop keeps the goldens small and
independent of the test scene's rendering.

Tolerance: a pixel is over when one of its channels differs by more than PIXEL_TOLERANCE levels (of 255); the image
passes while at most OVER_SHARE of its pixels are over. SDF edges move by a level or two between GPUs and drivers and
pass; a moved or missing panel, row or text line fails.

HDR (GT4_HDR_TEST=1, display.hdr_paper_white = 200): the same hub on a 10-bit headless target, PQ-encoded. The
selected tab's chip (flat and opaque, the accent F29A1F) is found where the SDR capture has exactly that colour, and
read at the same pixels in HDR: each channel must be the overlay's encode (sRGB decoded, BT.709 to BT.2020, times
200 / 10000, PQ; stored in 10 bits, written to the 8-bit capture) within +-HDR_TOLERANCE of 255, which is +-4 of the
target's 1023 codes (the capture is 8-bit; a 16-bit capture of a 10-bit target would tighten it). The same encode
gives white at 200 cd/m2 as PQ 0.5807, 148 of 255 (the plinths read exactly that before the stand-ins replaced
them).

Skips (exit 77) where gt4_host finds no Vulkan driver or device (CI runners). --update rewrites the goldens.
    ui_page_goldens.py HOST [--update]
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDENS = os.path.join(ROOT, 'tests', 'goldens', 'ui')
SIZES = [(1280, 720), (2560, 1440), (3840, 2160)]
PIXEL_TOLERANCE = 24
OVER_SHARE = 0.003
PAPER_WHITE = 200.0
ACCENT = (0xF2, 0x9A, 0x1F)   # the selected tab's chip (menu_layout.h's palette kAccent)
HDR_TOLERANCE = 1
NO_GPU = ('Vulkan loader is not installed', 'vkCreateInstance: Vulkan result -9', 'No Vulkan device found')


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # Four whitespace-separated header tokens, then exactly one whitespace byte, then the pixels (which may start
    # with bytes that look like whitespace: no split on the rest).
    tokens, at = [], 0
    while len(tokens) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[at:end])
        at = end
    if tokens[0] != b'P6' or tokens[3] != b'255':
        raise ValueError(path + ': not an 8-bit P6')
    width, height = int(tokens[1]), int(tokens[2])
    pixels = data[at + 1:at + 1 + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError(path + ': short pixel data')
    return width, height, pixels


def write_png(path, width, height, rgb):
    raw = b''.join(b'\x00' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def read_png(path):
    """8-bit RGB PNGs as write_png makes them (and any filter type, for a golden an editor re-saved)."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(path + ': not a PNG')
    at, idat, width, height = 8, b'', 0, 0
    while at < len(data):
        length, kind = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b'IHDR':
            width, height, depth, colour = struct.unpack('>IIBB', body[:10])
            if depth != 8 or colour != 2:
                raise ValueError(path + ': not 8-bit RGB')
        elif kind == b'IDAT':
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 3
    out = bytearray(height * stride)
    prev = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, bytes(out)


def draw(host, width, height, path, hdr=False):
    """The hub over the test scene, frame 2, captured; None when there is no GPU here."""
    env = dict(os.environ, GT4_UI_PAGE_TEST='hub')
    env.pop('GT4_HDR_TEST', None)
    if hdr:
        env['GT4_HDR_TEST'] = '1'
    command = [host, '--headless', '--frames', '2', '--size', '%dx%d' % (width, height), '--preset', 'original',
               '--set', 'aspect.ratio=window', '--set', 'debug.overlay=off', '--set', 'display.hdr_paper_white=200',
               '--no-audio', '--capture', path]
    result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=300)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        if any(marker in output for marker in NO_GPU):
            return None
        raise RuntimeError('gt4_host failed (%d):\n%s' % (result.returncode, output[-4000:]))
    if 'GT4_UI_PAGE_TEST:' in output:
        raise RuntimeError('the hub was not drawn:\n' + output[-2000:])
    return read_ppm(path)


def hdr_code(srgb8):
    """The overlay's HDR10 encode of an sRGB colour (overlay.frag, common.glsl), as the 8-bit capture stores it."""
    def decode(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (decode(c) for c in srgb8)
    bt2020 = (0.6274 * r + 0.3293 * g + 0.0433 * b, 0.0691 * r + 0.9195 * g + 0.0114 * b,
              0.0164 * r + 0.0880 * g + 0.8956 * b)
    m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
    out = []
    for y in bt2020:
        p = min(max(y * PAPER_WHITE / 10000.0, 0.0), 1.0) ** m1
        pq = ((c1 + c2 * p) / (1.0 + c3 * p)) ** m2
        ten = int(pq * 1023.0 + 0.5)                 # UNORM 10-bit
        out.append((ten * 255 + 511) // 1023)        # the capture's 8-bit conversion (renderer.cpp saveCapture)
    return tuple(out)


def positions_of(rgb, colour):
    target = bytes(colour)
    return [i for i in range(0, len(rgb), 3) if rgb[i:i + 3] == target]


def compare(name, width, height, rgb, golden):
    gw, gh, grgb = golden
    if (gw, gh) != (width, height):
        return '%s: %dx%d against a %dx%d golden' % (name, width, height, gw, gh)
    over = 0
    worst = 0
    for i in range(0, len(rgb), 3):
        d = max(abs(rgb[i] - grgb[i]), abs(rgb[i + 1] - grgb[i + 1]), abs(rgb[i + 2] - grgb[i + 2]))
        worst = max(worst, d)
        over += d > PIXEL_TOLERANCE
    share = over / (width * height)
    print('%s: %d pixel(s) over %d levels (%.4f %%, the cap %.2f %%), largest difference %d'
          % (name, over, PIXEL_TOLERANCE, share * 100, OVER_SHARE * 100, worst))
    return None if share <= OVER_SHARE else '%s: %.4f %% of pixels over the tolerance' % (name, share * 100)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    host, update = sys.argv[1], '--update' in sys.argv[2:]
    failures = []
    with tempfile.TemporaryDirectory() as work:
        for width, height in SIZES:
            name = 'hub_%dp' % height
            shot = draw(host, width, height, os.path.join(work, name + '.ppm'))
            if shot is None:
                print('ui_page_goldens: no Vulkan driver or device here; skipping')
                return 77
            w, h, rgb = shot
            if height == 720:
                chip = positions_of(rgb, ACCENT)
            golden = os.path.join(GOLDENS, name + '.png')
            if update:
                os.makedirs(GOLDENS, exist_ok=True)
                write_png(golden, w, h, rgb)
                print('wrote ' + golden)
                continue
            if not os.path.exists(golden):
                failures.append(name + ': no golden (run with --update)')
                continue
            problem = compare(name, w, h, rgb, read_png(golden))
            if problem:
                failures.append(problem)
                write_png(os.path.join(ROOT, 'build', name + '_actual.png'), w, h, rgb)
        expected = hdr_code(ACCENT)
        print('hub_hdr: white at %g cd/m2 encodes as %s; the chip %s as %s'
              % (PAPER_WHITE, hdr_code((255, 255, 255)), ACCENT, expected))
        if len(chip) < 1000:
            failures.append('hub_720p: the selected tab chip has %d pixels, too few to measure' % len(chip))
        else:
            hdr = draw(host, 1280, 720, os.path.join(work, 'hub_hdr.ppm'), hdr=True)[2]
            measured, count = Counter(hdr[i:i + 3] for i in chip).most_common(1)[0]
            print('hub_hdr: the chip reads %s on %d of its %d pixels' % (tuple(measured), count, len(chip)))
            # (+-1 of 255 in the capture is +-4 of the target's 1023 codes: the capture is 8-bit, renderer.cpp)
            if any(abs(m - e) > HDR_TOLERANCE for m, e in zip(measured, expected)) or count < len(chip) * 0.9:
                failures.append('hub_hdr: the chip reads %s, not %s +-%d of 255 (+-4 of 1023)'
                                % (tuple(measured), expected, HDR_TOLERANCE))
    for failure in failures:
        print('FAIL: ' + failure)
    print('ui_page_goldens: ' + ('%d failure(s)' % len(failures) if failures else 'all checks passed'))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
