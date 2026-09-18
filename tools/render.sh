#!/bin/sh
# Render the Prose brand SVGs to PNG with QuickLook (qlmanage), cropping the
# square QuickLook canvas back to each SVG's aspect ratio (content is centred,
# so a centre crop is exact).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/brand/png"
mkdir -p "$OUT"

for svg in "$ROOT"/brand/svg/*.svg; do
	name=$(basename "$svg" .svg)
	size=3840
	case "$name" in
	prose-logo-*|prose-mark*|boot-splash) size=960 ;;
	prose-lockup-*) size=1920 ;;
	esac
	rm -f "$OUT/$name.png"
	qlmanage -t -s "$size" -o "$OUT" "$svg" >/dev/null 2>&1
	mv "$OUT/$name.svg.png" "$OUT/$name.png"

	# centre-crop to the viewBox aspect
	vb=$(sed -n 's/.*viewBox="[0-9.]* [0-9.]* \([0-9.]*\) \([0-9.]*\)".*/\1 \2/p' "$svg" | head -1)
	w=${vb% *}; h=${vb#* }
	cw=$(sips -g pixelWidth "$OUT/$name.png" | awk '/pixelWidth/{print $2}')
	ch=$(sips -g pixelHeight "$OUT/$name.png" | awk '/pixelHeight/{print $2}')
	th=$(( cw * h / w ))
	if [ "$th" -le "$ch" ] && [ "$th" -gt 0 ]; then
		sips -c "$th" "$cw" "$OUT/$name.png" >/dev/null
	elif [ "$cw" -gt 0 ]; then
		tw=$(( ch * w / h ))
		sips -c "$ch" "$tw" "$OUT/$name.png" >/dev/null
	fi
done
ls -lh "$OUT"
