#!/usr/bin/env python3
"""Inventory a local gt4-decomp checkout; do not copy or compile upstream files."""
import argparse
import json
import subprocess
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/upstream-inventory.json"))
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    if not (upstream / "README.md").is_file() or not (upstream / "src").is_dir():
        parser.error("Expected a gt4-decomp checkout containing README.md and src/")
    commit = subprocess.run(["git", "-C", str(upstream), "rev-parse", "HEAD"],
                            capture_output=True, text=True, check=True).stdout.strip()
    paths = sorted(p.relative_to(upstream).as_posix()
                   for p in (upstream / "src").rglob("*")
                   if p.is_file() and p.suffix.lower() in (".c", ".cc", ".cpp", ".h", ".hpp"))
    report = {"repository": "gustavocanepa/gt4-decomp", "commit": commit,
              "source_count": len(paths), "source_files": paths,
              "note": "Inventory only. No source files imported or certified portable."}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Inventoried {len(paths)} files from {commit[:12]} -> {args.output}")

if __name__ == "__main__":
    main()
