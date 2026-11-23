#!/usr/bin/env python3
"""spanlevel.py MAME.wav REPLAY.wav [KNEE] -- the replay's level over the
seconds where MAME's recording has sound (wavlevel.py's measures: rms with
each second's DC taken out, the median second, the loudest second, the
peak). KNEE: the samples the DAC's soft knee took in (cart_host's
"clipped samples"), given as a share of the lists' samples in that span.

The replay (wavalign.py's: lined up with MAME's, at its rate) is the lists
on MAME's ColecoVision. The DAC's three tones are parked at a period of 1,
a 112 kHz carrier, and MAME's resampling leaves some of it at about 17.6
kHz, -45 dB, all the time, sound or none. A real console's carrier is far
above hearing. So wavlevel.py counts the replay's silent stretches (a game
over, the attract mode: silent in MAME) as seconds of sound, at about -42
dBFS, and its rms comes out low. This measures the same span as MAME's
recording instead, and says what the rest holds."""
import math, struct, sys, wave

SILENT_DB = -60.0                       # (wavlevel.py's)


def samples(path):
    w = wave.open(path)
    ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    if width != 2:
        sys.exit("spanlevel.py: %s isn't 16-bit" % path)
    d = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    if ch == 2:
        d = [(d[i] + d[i + 1]) / 2 for i in range(0, len(d), 2)]
    return [x / 32768.0 for x in d], rate


def seconds(s, rate):
    out = []
    for k in range(len(s) // rate):
        seg = s[k * rate:(k + 1) * rate]
        m = sum(seg) / len(seg)
        out.append((sum((x - m) ** 2 for x in seg) / len(seg), max(abs(x) for x in seg)))
    return out


def db(x):
    return 10 * math.log10(x) if x > 0 else float("-inf")


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    m, mr = samples(sys.argv[1])
    r, rr = samples(sys.argv[2])
    ms, rs = seconds(m, mr), seconds(r, rr)
    n = min(len(ms), len(rs))
    span = [k for k in range(n) if db(ms[k][0]) >= SILENT_DB]
    rest = [k for k in range(n) if db(ms[k][0]) < SILENT_DB]
    if not span:
        sys.exit("spanlevel.py: MAME's recording has no sound")
    for name, sec in (("MAME", ms), ("replay", rs)):
        ms_sq = sum(sec[k][0] for k in span) / len(span)
        levels = sorted(db(sec[k][0]) for k in span)
        print("%-7s over MAME's %d seconds with sound: rms %.1f  median %.1f  loudest %.1f  peak %.1f dBFS"
              % (name, len(span), db(ms_sq), levels[len(levels) // 2], levels[-1],
                 20 * math.log10(max(sec[k][1] for k in span) or 1e-9)))
    if len(sys.argv) == 4 and sys.argv[3]:
        n_span = len(span) * 100 * 3579545 / 60036          # the lists' samples (5,962 a second)
        print("the DAC's soft knee took %s samples: %.1f%% of the %d in those seconds"
              % (sys.argv[3], 100.0 * int(sys.argv[3]) / n_span, n_span))
    if rest:
        print("replay  in MAME's %d silent seconds: %.1f dBFS rms (the parked tones' carrier, as MAME resamples it)"
              % (len(rest), db(sum(rs[k][0] for k in rest) / len(rest))))


main()
