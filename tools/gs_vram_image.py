"""PNG of a frame buffer in a GS memory dump (host GT4_VRAM_DUMP).

    py tools/gs_vram_image.py VRAM.bin FBP FBW HEIGHT OUT.png [--alpha]

FBP in 2048-word pages (hex), FBW in units of 64 pixels; PSMCT32/24 layout only. --alpha writes
the alpha channel as a grey image instead of the colour."""
import sys

import numpy as np
from PIL import Image

# PSMCT32 block order within a page (8 x 4 blocks of 8 x 8 pixels)
BLOCKS = [[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23],
          [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]]
# word within a column (8 x 2 pixels)
COLUMN = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15]]


def addresses(fbp, fbw, width, height):
    y, x = np.mgrid[0:height, 0:width]
    page = fbp + (y // 32) * fbw + x // 64
    px, py = x % 64, y % 32
    block = np.array(BLOCKS)[py // 8, px // 8]
    bx, by = px % 8, py % 8
    word = np.array(COLUMN)[by % 2, bx] + (by // 2) * 16
    return (page * 2048 + block * 64 + word) % (1 << 20)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path, fbp, fbw, height, out = args[0], int(args[1], 16), int(args[2]), int(args[3]), args[4]
    words = np.fromfile(path, dtype="<u4")
    px = words[addresses(fbp, fbw, fbw * 64, height)]
    if "--alpha" in sys.argv:
        Image.fromarray((px >> 24).astype(np.uint8), "L").save(out)
        return
    rgb = np.stack([px & 0xFF, (px >> 8) & 0xFF, (px >> 16) & 0xFF], axis=-1).astype(np.uint8)
    Image.fromarray(rgb, "RGB").save(out)


if __name__ == "__main__":
    main()
