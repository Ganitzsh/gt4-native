#!/usr/bin/env python3
"""sheet.py OUT.png PPM...: the frames side by side, two per row, each at 640 x 360."""
import sys

from PIL import Image

sys.path.insert(0, __import__("os").path.dirname(__file__))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402
import numpy as np  # noqa: E402
read_ppm = lambda p: Image.fromarray(fm.read_ppm(str(p)).astype(np.uint8))
from pathlib import Path

ims = [read_ppm(Path(p)).resize((640, 360)) for p in sys.argv[2:]]
rows = (len(ims) + 1) // 2
sheet = Image.new("RGB", (1280, 360 * rows))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % 2) * 640, (i // 2) * 360))
sheet.save(sys.argv[1])
