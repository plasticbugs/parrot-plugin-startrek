// arm_bench.c -- the cart's code for armcount.py: what a list (a TV frame)
// costs on the Cortex-M33, counted in instructions on Unicorn. Built bare
// (no SDK) with the plugin's flags (armcount.sh). The Python side puts the
// set's files in the arrays below, calls bench_init() once and
// bench_list() for each list. The game plays itself: st_host's bot (two
// coins, 1P start, then turning, phasers, photon torpedoes, impulse and
// warp on a pattern). (Time Pilot's.)
#include <string.h>

#include "../../src/st.h"
#include "../../src/stcv.h"
#include "../../src/strun.h"

uint8_t bench_maincpu[0xc000], bench_speech_cpu[0x800], bench_speech_data[0x2000], bench_sine[0x400];
uint8_t bench_behind;                   // (armcount.py's queue; unused here)
uint32_t bench_len, bench_pictures, bench_hurried;
uint8_t bench_out[STCV_LIST_MAX];       // the last list (armcount.py reads it back)
static stcv_t cv;
static strun_t run;

static void on_vblank(void) {
    strun_vblank();
    uint32_t n = st_frames;             // (st_host's bot_value())
    if (n == 40 || n == 80) st_in.d7d6 &= (uint8_t)~1;
    if (n == 44 || n == 84) st_in.d7d6 |= 1;
    if (n == 140) st_in.fc |= 1;
    if (n == 144) st_in.fc &= (uint8_t)~1;
    if (n >= 200) {
        int t = (int)n - 200;
        static const int8_t turn[8] = {3, 0, -5, 2, 0, 8, -2, -9};
        st_in.dial = turn[(t / 37) % 8];
        uint8_t fc = 0;
        if ((t % 9) < 3) fc |= 0x08;
        if ((t % 150) < 4) fc |= 0x04;
        if ((t % 400) < 90) fc |= 0x10;
        if ((t % 1300) > 1250) fc |= 0x20;
        st_in.fc = fc;
    }
}

void bench_init(void) {
#if defined(__arm__)
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88;     // the FPU on (CP10, CP11)
    *cpacr |= 0xFu << 20;
#endif
    static st_roms_t roms;
    roms = (st_roms_t){bench_maincpu, bench_speech_cpu, bench_speech_data, bench_sine};
    st_in.d3d2 = ST_D3D2;
    st_in.d1d0 = ST_D1D0;
    stcv_init(&cv);
    st_init(&roms);
    strun_start(&run, &cv);
    st_on_vblank = on_vblank;
}

void bench_list(void) {
    bench_len = (uint32_t)strun_list(&run, bench_out);
    bench_pictures = run.pictures;
    bench_hurried = run.waited;
}

void _exit(int x) { (void)x; for (;;); }
