"""bench_summary.py <pgr3.log> <result.json>: summarises an AI-driven benchmark race (tools/benchmark.ps1).

Reads the light performance log ("perf lite:" lines, one a second, ReGPU REGPU_PERF_LITE) between
"benchmark: measuring" and "benchmark: done" (src/hooks/grid_test.cpp) and writes the averages as JSON, plus a short
text summary on stdout.
"""
import json
import re
import statistics
import sys

LINE = re.compile(r"perf lite: ([\d.]+) fps, ([\d.]+) ms/frame \(p99 ([\d.]+) ms, max ([\d.]+) ms\) \| (\d+) draws/frame"
                  r" \| GPU ([\d.]+) ms/frame \| draw thread (\d+)% busy")


def main(log_path, out_path):
    rows, measuring, window = [], False, None
    for line in open(log_path, encoding="utf-8", errors="replace"):
        if "benchmark: measuring" in line:
            measuring = True
            m = re.search(r"measuring (\d+) s", line)
            window = int(m.group(1)) if m else None
            continue
        if "benchmark: done" in line:
            break
        if measuring:
            m = LINE.search(line)
            if m:
                rows.append([float(g) for g in m.groups()])
    if not rows:
        print("no perf lite lines inside the measuring window")
        json.dump({"ok": False}, open(out_path, "w"))
        return 1
    fps = [r[0] for r in rows]
    result = {
        "ok": True,
        "window_s": window,
        "seconds": len(rows),
        "fps_avg": round(statistics.fmean(fps), 1),
        "fps_lowest_second": round(min(fps), 1),
        "frame_ms_avg": round(statistics.fmean(r[1] for r in rows), 2),
        "frame_ms_p99_median": round(statistics.median(r[2] for r in rows), 2),
        "frame_ms_worst": round(max(r[3] for r in rows), 2),
        "draws_avg": round(statistics.fmean(r[4] for r in rows)),
        "gpu_ms_avg": round(statistics.fmean(r[5] for r in rows), 2),
        "draw_thread_busy_avg": round(statistics.fmean(r[6] for r in rows)),
    }
    json.dump(result, open(out_path, "w"), indent=1)
    print("%d s measured: %.1f fps avg (lowest second %.1f), p99 %.2f ms (median of seconds), worst %.2f ms, "
          "%d draws/frame, GPU %.2f ms/frame, draw thread %d%% busy" % (
              result["seconds"], result["fps_avg"], result["fps_lowest_second"], result["frame_ms_p99_median"],
              result["frame_ms_worst"], result["draws_avg"], result["gpu_ms_avg"], result["draw_thread_busy_avg"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
