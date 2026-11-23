#!/bin/sh
# trace.sh [main|usb|speech] [seconds] -- a CPU against MAME's, instruction
# by instruction.
#
# MAME's debugger (headless, -debug -debugger none) traces the CPU from
# its start (the others from the second frame): each instruction's registers and the CPU's total
# cycles. st_host writes the same from the model (-trace, -utrace,
# -strace). Every line must be the same. Everything goes under $TMPDIR
# (ST_WORK). ST_ROMS is the startrek.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
cpu=${1:-main}
secs=${2:-2}
work=${ST_WORK:-${TMPDIR:-/tmp}/startrek-test}
set_dir=$("$here/setup.sh")
out="$work/trace-$cpu"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/st_host" "$here/st_host.c" "$src/st.c" "$src/stz80.c" "$src/st48usb.c" "$src/st48sp.c"
case $cpu in
main) tag=:maincpu; opt=-trace
      fmt="@ %04X %04X %04X %04X %04X %04X %04X %04X %d"; regs="pc,sp,af,bc,de,hl,ix,iy,totalcycles" ;;
usb) tag=:usbsnd:ourcpu; opt=-utrace; fmt="@ %03X %02X %d"; regs="pc,a,totalcycles" ;;
speech) tag=:speech:cpu; opt=-strace; fmt="@ %03X %02X %d"; regs="pc,a,totalcycles" ;;
*) echo "trace.sh: main, usb or speech" >&2; exit 2 ;;
esac
"$out/st_host" "$set_dir" -frames $(( secs * 40 + 10 )) $opt "$out/model.tr" 2>/dev/null
# (tracelog writes for the debugger's visible CPU, which the debugger's
# start makes the main CPU: so the main CPU is traced from the start, the
# others from the second frame, made the visible one then)
cat > "$out/trace.lua" <<LUA
if not _G.st_traced then
  _G.st_traced = true
  local function start()
    manager.machine.debugger.visible_cpu = manager.machine.devices['$tag']
    manager.machine.debugger:command('trace $out/mame.tr,$tag,noloop,{tracelog "$fmt\\\\n",$regs}')
  end
  if '$cpu' == 'main' then start() else
    _G.st_n = 0
    _G.st_sub = emu.register_frame_done(function()
      _G.st_n = _G.st_n + 1
      if _G.st_n == 2 then start() end
    end)
  end
end
LUA
rp=$(mktemp -d "${TMPDIR:-/tmp}/strp.XXXXXX")
cp "$ST_ROMS" "$rp/startrek.zip"
mkdir "$rp/cfg" "$rp/nvram"
SDL_VIDEODRIVER=dummy mame startrek -rompath "$rp" -debug -debugger none \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run "$secs" \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
    -autoboot_delay 0 -autoboot_script "$out/trace.lua" >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"

python3 -I - "$out" "$cpu" <<'PY'
import sys
out, cpu = sys.argv[1], sys.argv[2]

def at_lines(path):
    try:
        with open(path) as f:
            return [l.split() for l in f if l.startswith("@ ")]
    except FileNotFoundError:
        return []

mame = at_lines(f"{out}/mame.tr")[:-1]          # (the last may be cut off by the exit)
model = at_lines(f"{out}/model.tr")
if not mame:
    print(f"{cpu}: MAME wrote no trace (see mame.log)"); sys.exit(1)
start = next((i for i, l in enumerate(model) if l == mame[0]), None)
if start is None:
    print(f"{cpu}: MAME's first line isn't in the model's: {' '.join(mame[0])}"); sys.exit(1)
n = min(len(mame), len(model) - start)
for i in range(n):
    if mame[i] != model[start + i]:
        print(f"{cpu}: instruction {i} differs (model line {start + i})")
        for k in range(max(0, i - 4), i + 1):
            print(f"  MAME  {' '.join(mame[k])}\n  model {' '.join(model[start + k])}")
        sys.exit(1)
print(f"{cpu}: {n} instructions the same, registers and cycle counts (from cycle {mame[0][-1]})")
PY
