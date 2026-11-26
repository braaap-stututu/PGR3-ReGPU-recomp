#!/bin/bash
# Deck (session 8): per-frame share of magenta pixels (R>150, B>150, G < 0.55 R) in each burst dir's raw 640x360 RGBA frames.
# Writes <outdir>/magenta.txt: burst frame permille. Frames over THRESH permille are kept as PNG in <outdir>/hits/.
OUT="$1"; THRESH="${2:-0.5}"; mkdir -p "$OUT/hits"; : > "$OUT/magenta.txt"
while IFS= read -r d; do
  n=$(basename "$d")
  find "$d" -name 's*.raw' | sort -V | tr '\n' '\0' | xargs -0 cat 2>/dev/null | ffmpeg -loglevel error -f rawvideo -pix_fmt rgba -s 640x360 -i - \
    -vf "format=gbrp,geq=r='255*gt(r(X,Y),150)*gt(b(X,Y),150)*lt(g(X,Y),0.55*r(X,Y))':g='255*gt(r(X,Y),150)*gt(b(X,Y),150)*lt(g(X,Y),0.55*r(X,Y))':b='255*gt(r(X,Y),150)*gt(b(X,Y),150)*lt(g(X,Y),0.55*r(X,Y))',format=gray,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-" \
    -f null - | awk -v n="$n" '/frame:/{split($2,a,":"); f=a[2]} /YAVG/{split($0,a,"="); printf "%s %d %.3f\n", n, f, a[2]/255*1000}' >> "$OUT/magenta.txt"
done < <(find "$OUT" -maxdepth 1 -type d -name 'b[0-9]*' | sort -V)
awk -v t="$THRESH" '$3>t' "$OUT/magenta.txt" | while read n f p; do
  ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s 640x360 -i "$OUT/$n/s$f.raw" "$OUT/hits/${n}_s$f.png"
done
echo "frames: $(wc -l < "$OUT/magenta.txt"), over $THRESH permille: $(awk -v t="$THRESH" '$3>t' "$OUT/magenta.txt" | wc -l)"
