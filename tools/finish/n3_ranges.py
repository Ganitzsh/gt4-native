import os, re, subprocess, statistics
from pathlib import Path
WT = Path(r"C:\Users\julie\gt4-wt-land")
cap = r"C:\Users\julie\gt4-native-game\build\caps\lap_ssr5f_boot.cap"
env = dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_GSN_SCENE_SPLIT="1", GT4_SCENE_CACHE_LOG="1",
           GT4_N3_VERDICTS="1", GT4_N3_LOG="1")
r = subprocess.run([str(WT / "build/host-windows-Release/gt4_host.exe"), "--headless", "--replay", cap, "--frames", "20200",
                    "--fixed-step", "--size", "640x360", "--preset", "original", "--set", "display.frame_interpolation=off",
                    "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"],
                   capture_output=True, text=True, errors="replace", env=env, creationflags=subprocess.CREATE_NO_WINDOW)
vb, judged, cpu = 0, {}, {}
sample = None
for line in (r.stdout + r.stderr).splitlines():
    m = re.match(r"scene cache: vblank (\d+): .*?\(cpu ([\d.]+) us\)", line)
    if m:
        vb = int(m.group(1))
        cpu.setdefault(vb // 2000, []).append(float(m.group(2)))
        if sample is None and vb > 9000:
            sample = line[:600]
    elif line.startswith("n3 verdict:"):
        judged[vb // 2000] = judged.get(vb // 2000, 0) + 1
print("sample:", sample)
from collections import Counter
rows = re.findall(r"n3 verdict: texture (\w+) material (\d+) item (\d+) (.*)", r.stdout + r.stderr)
print("lines", len(rows), "textures", len({t for t, m, i, x in rows}), "materials", len({m for t, m, i, x in rows}),
      "(item, measures)", len({(i, x) for t, m, i, x in rows}), "measures", len({x for t, m, i, x in rows}))
bym = Counter(x for t, m, i, x in rows)
for x, n in bym.most_common(8):
    ts = {t for t, m, i, y in rows if y == x}
    print(f"  {n:6d} x {x}   ({len(ts)} textures)")
dbg = re.findall(r"n3 dbg: object (\w+) flags (\w+) view (\d+) mesh (\w+) pos (\S+) (\S+) (\S+) m00 (\S+) count (\d+) unpack (\w+)", r.stdout + r.stderr)
print("dbg", len(dbg), "objects", len({d[0] for d in dbg}), "meshes", len({d[3] for d in dbg}), "positions", len({d[4:7] for d in dbg}),
      "flags", Counter(d[1] for d in dbg).most_common(6), "counts", Counter(d[8] for d in dbg).most_common(5),
      "unpack", Counter(d[9] for d in dbg).most_common(5))
print("objects top", Counter(d[0] for d in dbg).most_common(5))
for d in dbg[-14:]:
    print("  dbg late:", d)
tail = rows[-40:]
for t, m, i, x in tail[:12]:
    print("  late:", t, m, i, x)
for k in sorted(cpu):
    xs = sorted(cpu[k])
    print(f"vblanks {k*2000:5d}-{k*2000+1999}: {len(xs):4d} scene frames, judged {judged.get(k, 0):6d}, "
          f"cpu mean {statistics.mean(xs):6.1f} us, p95 {xs[int(0.95*(len(xs)-1))]:6.1f}, max {xs[-1]:8.1f}")
