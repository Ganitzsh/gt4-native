#!/usr/bin/env python3
"""vol_peek.py: page headers and slot tails of GT4.VOL's TOC pages (vol_ls.py's reader), to learn the node layout."""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
import vol_ls  # noqa: E402  (stops at its own entries() on import: guarded below)
