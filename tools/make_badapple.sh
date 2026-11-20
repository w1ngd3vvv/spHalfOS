#!/bin/sh
# Convert badapple.mp4 into the compact 1-bit .bav format played on spHalfOS.
# Requires ffmpeg and build/mkbadvideo (built by the Makefile).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

if [ ! -f badapple.mp4 ]; then
    echo "make_badapple: badapple.mp4 not found in $ROOT" >&2
    exit 1
fi
if [ ! -x build/mkbadvideo ]; then
    echo "make_badapple: build/mkbadvideo missing (run make badapple-data)" >&2
    exit 1
fi

mkdir -p assets
tmp=assets/.badapple.bav.tmp
trap 'rm -f "$tmp"' EXIT INT TERM

echo "make_badapple: decoding badapple.mp4 (256x192, 30 fps, keyframe every 2 s)"
ffmpeg -v error -i badapple.mp4 \
    -vf "fps=30,scale=256:192:flags=lanczos,format=gray" \
    -f rawvideo - \
    | ./build/mkbadvideo "$tmp" 256 192 30 60

mv "$tmp" assets/badapple.bav
trap - EXIT INT TERM
echo "make_badapple: wrote assets/badapple.bav ($(stat -c%s assets/badapple.bav) bytes)"
