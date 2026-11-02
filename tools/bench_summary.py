#!/usr/bin/env python3
"""Summarise --bench logs: one row per model, median across repeated runs.

usage: tools/bench_summary.py <dir with <Model>_run<N>.log files>
"""
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def parse_log(text):
    row = {}
    m = re.search(r"# Vertices: (\d+)", text)
    row["vertices"] = int(m.group(1)) if m else None
    m = re.search(r"# Triangles: (\d+)", text)
    row["triangles"] = int(m.group(1)) if m else None
    m = re.search(r"Load: MTL\+textures ([\d.]+) ms, OBJ parse ([\d.]+) ms, GPU upload ([\d.]+) ms", text)
    if m:
        row["mtl_ms"], row["obj_ms"], row["upload_ms"] = map(float, m.groups())
    m = re.search(r"Load: glTF parse ([\d.]+) ms, images ([\d.]+) ms, GPU upload ([\d.]+) ms", text)
    if m:
        row["obj_ms"], row["mtl_ms"], row["upload_ms"] = map(float, m.groups())
    for name in ("cpu_frame", "cpu_render", "gpu_mesh", "gpu_total"):
        m = re.search(rf"^\s+{name}\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", text, re.M)
        if m:
            row[f"{name}_mean"], row[f"{name}_median"], row[f"{name}_p95"] = map(float, m.groups())
    m = re.search(r"draw calls/frame: (\d+)", text)
    row["draws"] = int(m.group(1)) if m else None
    m = re.search(r"renderer: (.*)", text)
    row["renderer"] = m.group(1).strip() if m else ""
    m = re.search(r"viewport: (\S+) \| vsync: (\S+)", text)
    row["viewport"] = f"{m.group(1)}, vsync {m.group(2)}" if m else ""
    return row


def main():
    folder = Path(sys.argv[1])
    runs = defaultdict(list)
    for log in sorted(folder.glob("*_run*.log")):
        model = log.name.rsplit("_run", 1)[0]
        runs[model].append(parse_log(log.read_text()))

    def med(rows, key):
        values = [r[key] for r in rows if r.get(key) is not None]
        return statistics.median(values) if values else float("nan")

    first_runs = next(iter(runs.values()))
    first = first_runs[0]
    print(f"Machine: {first['renderer']}; viewport {first['viewport']}; "
          f"1000 frames after 100 warm-up; median of {len(first_runs)} runs.\n")
    print("| Model | Vertices | Triangles | Parse ms | Textures ms | Draws | CPU frame median ms | CPU frame p95 ms | GPU total median ms |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for model, rows in runs.items():
        print(f"| {model} | {rows[0]['vertices']:,} | {rows[0]['triangles']:,} | {med(rows, 'obj_ms'):.1f} | "
              f"{med(rows, 'mtl_ms'):.1f} | {rows[0]['draws']} | {med(rows, 'cpu_frame_median'):.3f} | "
              f"{med(rows, 'cpu_frame_p95'):.3f} | {med(rows, 'gpu_total_median'):.3f} |")


if __name__ == "__main__":
    main()
