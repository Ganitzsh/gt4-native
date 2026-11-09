import json, subprocess, sys
from statistics import median
S = "C:/Users/julie/gt4-lane-F/build/sessions/"
sets = {"replay lap_ssr5r (owner settings, 1280x720)": S + "bands_ssr5r",
        "replay lap_csr5 (owner settings, 1280x720)": S + "bands_csr5",
        "live B-spec lap_ssr5r_prog shots": S + "lap_ssr5r_prog/frames",
        "live A-spec views, pack on": S + "view_ssr5a_on/frames",
        "live A-spec views, pack off": S + "view_ssr5a_off/frames"}
for name, d in sets.items():
    out = subprocess.run([sys.executable, "C:/Users/julie/gt4-lane-F/tools/band_measure.py", "--json", d],
                         capture_output=True, text=True).stdout
    rows = [json.loads(l) for l in out.splitlines() if l.strip()]
    rows = [r for r in rows if r["frame"].startswith(("f0", "live", "view"))]   # frames only: no menus, no sheets
    if not rows:
        print(name, "no frames"); continue
    sc = sorted(r["score"] for r in rows)
    p95 = sc[min(len(sc) - 1, int(0.95 * len(sc)))]
    peaks = sorted((max((abs(x[2]) for x in r["runs"]), default=0), r["frame"]) for r in rows)
    top = sorted(rows, key=lambda r: -r["score"])[:3]
    print(f"{name}: {len(rows)} frames; score median {median(sc):.4f}, p95 {p95:.4f}, max {sc[-1]:.4f}; "
          f"frames with no run {sum(1 for r in rows if not r['runs'])}; largest |peak| {peaks[-1][0]:.1f} ({peaks[-1][1]})")
    for r in top:
        print(f"    {r['frame']}: {r['score']:.4f}, runs {r['runs'][:4]}")