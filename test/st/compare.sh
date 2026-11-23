#!/bin/sh
# compare.sh [attract|game] [frames] -- the board model against MAME 0.288,
# frame by frame: the main RAM, the vector RAM, the USB's RAMs and the
# three CPUs' registers and RAM at every VBLANK.
#
#   attract  no inputs (default 4000 frames)
#   game     coins and a game played by st_host's bot (default 8000
#            frames); its inputs are recorded as a script and MAME plays
#            the same script
#
# MAME runs headless. Everything goes under $TMPDIR (ST_WORK). ST_ROMS is
# the startrek.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
name=${1:-attract}
case $name in
attract) frames=${2:-4000} ;;
game) frames=${2:-8000} ;;
*) echo "compare.sh: attract or game" >&2; exit 2 ;;
esac
work=${ST_WORK:-${TMPDIR:-/tmp}/startrek-test}
set_dir=$("$here/setup.sh")
out="$work/compare-$name"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/st_host" "$here/st_host.c" "$src/st.c" "$src/stz80.c" "$src/st48usb.c" "$src/st48sp.c"
if [ "$name" = game ]; then
    "$out/st_host" "$set_dir" -frames "$frames" -bot 1 -record "$out/script.txt" 2>/dev/null
else
    : > "$out/script.txt"
fi
"$out/st_host" "$set_dir" -frames "$frames" -script "$out/script.txt" -dump "$out/model.dump"
"$here/mamerun.sh" "$out" "$out/script.txt" "$frames" 400
python3 -I "$here/cmpdump.py" "$out/mame.dump" "$out/model.dump"
