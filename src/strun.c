// strun.c -- see strun.h.
#include "strun.h"

#include <string.h>

static strun_t *running;

void strun_vblank(void) {
    strun_t *r = running;
    if (!r) return;
    if (!r->cv->dirty || r->since >= STRUN_SKIP) {
        stcv_vectors(r->cv, st_segs, st_nsegs);
        r->pictures++;
        r->since = 0;
    } else {
        r->waited++;
    }
}

void strun_start(strun_t *r, stcv_t *cv) {
    memset(r, 0, sizeof *r);
    r->cv = cv;
    running = r;
    st_on_vblank = strun_vblank;
    stsnd_init();
    r->target = st_time();
}

size_t strun_list(strun_t *r, uint8_t *out) {
    r->lists++;
    r->target = ((uint64_t)r->lists * STCV_LIST_CYCLES_Q16 >> 16) + STRUN_LEAD;
    st_run_to(r->target);
    if (r->since < 255) r->since++;
    stsnd_pcm(r->pcm, STCV_PCM_N);
    stcv_sound_pcm(r->cv, r->pcm, STCV_PCM_N);
    return stcv_tv_frame(r->cv, out);
}
