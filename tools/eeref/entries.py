#!/usr/bin/env python3
"""eerecomp's --entries file: every function start of gt4-decomp's build/functions.csv, so that code reached
only through pointers (vtables, callbacks, constructor lists) is translated too.

    entries.py gt4-decomp/build/functions.csv OUT
"""
import csv
import sys

with open(sys.argv[1], newline='') as f, open(sys.argv[2], 'w', newline='\n') as out:
    for row in csv.DictReader(f):
        out.write('%08X\n' % int(row['address'], 16))
