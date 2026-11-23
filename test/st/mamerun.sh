#!/bin/sh
# mamerun.sh OUTDIR SCRIPT FRAMES [SNAPEVERY] -- MAME's startrek, headless,
# with stdump.lua: OUTDIR/mame.dump (and snapshots in OUTDIR/snap). ST_WAV
# set: MAME's sound to that file too (-wavwrite, 48 kHz). ST_ROMS is the
# set's zip.
#
# MAME runs headless, always: no window, no sound, as fast as it goes, and
# a time limit so it can't hang.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$1; script=$2; frames=$3; snapevery=${4:-0}
roms=${ST_ROMS:?ST_ROMS is the startrek.zip to use}
mkdir -p "$out"
rp=$(mktemp -d "${TMPDIR:-/tmp}/strp.XXXXXX")
cp "$roms" "$rp/startrek.zip"
mkdir "$rp/cfg" "$rp/nvram"
secs=$(( frames / 40 + 30 ))
ST_SCRIPT="$script" ST_DUMP="$out/mame.dump" ST_FRAMES="$frames" ST_SNAPEVERY="$snapevery" \
SDL_VIDEODRIVER=dummy mame startrek -rompath "$rp" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -frameskip 0 -skip_gameinfo -seconds_to_run "$secs" \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$out/snap" \
    -autoboot_delay 0 -autoboot_script "$here/stdump.lua" ${ST_WAV:+-wavwrite "$ST_WAV"} >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"
