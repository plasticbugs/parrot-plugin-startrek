#!/bin/sh
# soundcmp.sh [frames] -- the sound against MAME's, and how loud it is.
#
# A game (st_host's bot: two coins, one player, then all the controls on a
# pattern; its inputs recorded as a script) in MAME's startrek (headless,
# -wavwrite), in snd_host (stsnd.c's sound in MAME's units, at the lists'
# rate) and in cart_host (the plugin's lists, their samples as the DAC's
# levels). Each against MAME's (sndcmp.c), with tools/wavlevel.py's levels.
# With the ColecoVision BIOS where MAME finds it (MAME_ROMPATH, or MAME's
# own rompath), the lists also go through the SDK's real kernel on MAME's
# ColecoVision (tools/mame-replay.sh with MAME_WAV), as the console would
# play them, and that replay's loudness is the one to set against an
# ordinary cartridge's, about -22 dBFS rms (docs/testing.md). Everything
# goes under $TMPDIR (ST_WORK). ST_ROMS is the startrek.zip. (Time Pilot's.)
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
kit=$(cd "$here/../.." && pwd)
frames=${1:-3000}
work=${ST_WORK:-${TMPDIR:-/tmp}/startrek-test}
set_dir=$("$here/setup.sh")
out="$work/sound"
rm -rf "${out:?}"; mkdir -p "$out"
emu="$src/st.c $src/stz80.c $src/st48usb.c $src/st48sp.c"
# shellcheck disable=SC2086
cc -O2 -Wall -I"$src" -o "$out/st_host" "$here/st_host.c" $emu
# shellcheck disable=SC2086
cc -O2 -Wall -I"$src" -o "$out/snd_host" "$here/snd_host.c" $emu "$src/stsnd.c" -lm
# shellcheck disable=SC2086
cc -O2 -Wall -I"$src" -o "$out/cart_host" "$here/cart_host.c" $emu "$src/stsnd.c" "$src/stcv.c" "$src/strun.c" -lm
cc -O2 -Wall -o "$out/sndcmp" "$here/sndcmp.c" -lm
"$out/st_host" "$set_dir" -frames "$frames" -bot 1 -record "$out/script.txt" 2>/dev/null
"$out/snd_host" "$set_dir" "$out/script.txt" "$frames" "$out/model.wav"
ST_WAV="$out/mame.wav" "$here/mamerun.sh" "$out/mame" "$out/script.txt" "$frames"
lists=$(( frames * 3 / 2 ))             # (the game's 40 frames a second, the lists' 59.6)
"$out/cart_host" "$set_dir" -lists "$lists" -script "$out/script.txt" -wav "$out/levels.wav" -out "$out/lists.bin" > "$out/cart.log"
echo "the model (stsnd.c), at the lists' rate:"; "$out/sndcmp" "$out/mame.wav" "$out/model.wav"
echo "the lists, as the DAC's levels:"; "$out/sndcmp" "$out/mame.wav" "$out/levels.wav"
python3 -I "$kit/tools/wavlevel.py" "$out/mame.wav" "$out/model.wav"
if SDL_VIDEODRIVER=dummy mame -verifyroms coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} \
        -video none -videodriver dummy -window -nomaximize -sound none \
        -nothrottle -skip_gameinfo -seconds_to_run 10 >/dev/null 2>&1; then
    secs=$(( frames / 40 + 20 ))
    MAME_WAV="$out/replay-raw.wav" "$kit/tools/mame-replay.sh" "$out/lists.bin" "$out/replay" "$secs" > /dev/null
    python3 -I "$here/wavalign.py" "$out/mame.wav" "$out/replay-raw.wav" "$out/replay.wav" 60036/59736
    echo "the lists on MAME's ColecoVision:"; "$out/sndcmp" "$out/mame.wav" "$out/replay.wav"
    echo "how loud (an ordinary cartridge is about -22 dBFS rms):"
    python3 -I "$kit/tools/wavlevel.py" "$out/replay-raw.wav"
else
    echo "(no replay on MAME's ColecoVision: MAME can't find its BIOS, coleco.zip. MAME_ROMPATH names a folder with it.)"
fi
