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


FRAMES = re.compile(r"benchmark: frames (\d+), avg ([\d.]+) fps \([\d.]+ ms\), 1% low ([\d.]+) fps, 0\.1% low ([\d.]+) fps, "
                    r"p99 ([\d.]+) ms, p99\.9 ([\d.]+) ms, worst ([\d.]+) ms, stutters: over 2x median (\d+), over 8 ms (\d+), "
                    r"over 16\.7 ms (\d+)")


def main(log_path, out_path):
    rows, measuring, window, frames = [], False, None, None
    for line in open(log_path, encoding="utf-8", errors="replace"):
        m = FRAMES.search(line)
        if m:
            frames = m.groups()
        if "benchmark: measuring" in line:
            measuring = True
            m = re.search(r"measuring (\d+) s", line)
            window = int(m.group(1)) if m else None
            continue
        if "benchmark: done" in line:
            measuring = False
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
    if frames:  # per-frame (session 5): 1% lows and stutters
        n, avg, low1, low01, p99, p999, worst, o2x, o8, o16 = frames
        result.update({"frames": int(n), "fps_1pct_low": float(low1), "fps_01pct_low": float(low01), "frame_ms_p99": float(p99),
                       "frame_ms_p999": float(p999), "frame_ms_worst_frame": float(worst), "stutters_2x_median": int(o2x),
                       "stutters_8ms": int(o8), "stutters_16ms": int(o16)})
    json.dump(result, open(out_path, "w"), indent=1)
    print("%d s measured: %.1f fps avg (lowest second %.1f), p99 %.2f ms (median of seconds), worst %.2f ms, "
          "%d draws/frame, GPU %.2f ms/frame, draw thread %d%% busy" % (
              result["seconds"], result["fps_avg"], result["fps_lowest_second"], result["frame_ms_p99_median"],
              result["frame_ms_worst"], result["draws_avg"], result["gpu_ms_avg"], result["draw_thread_busy_avg"]))
    if frames:
        print("per frame: 1%% low %.1f fps, 0.1%% low %.1f fps, p99 %.2f ms, p99.9 %.2f ms, worst %.2f ms, stutters >2x median %d, "
              ">8 ms %d, >16.7 ms %d" % (result["fps_1pct_low"], result["fps_01pct_low"], result["frame_ms_p99"], result["frame_ms_p999"],
                                         result["frame_ms_worst_frame"], result["stutters_2x_median"], result["stutters_8ms"],
                                         result["stutters_16ms"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
