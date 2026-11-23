// strun: the cart's loop, a list at a time, shared by the plugin and the
// Mac tests so both run the board the same way. Portable. (Time Pilot's
// tprun.c.)
//
// Each list runs the board for the list's time (STCV_LIST_T of the
// console's T: 16.77 ms), to a cycle count kept from the start so nothing
// drifts; STCV_PCM_N samples of the sound go with the list (stsnd.c),
// taken STRUN_LEAD cycles behind the board so the speech samples and the
// sound board's writes for them are always made.
//
// The picture: the game draws 40 a second (the vector board's 40 Hz), the
// lists go at 59.6. At each VBLANK the vectors become the next picture,
// if the console has had the last one's bytes (or STRUN_SKIP lists have
// gone by since it); otherwise that frame of the game isn't shown and the
// game runs on at its own speed.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st.h"
#include "stcv.h"
#include "stsnd.h"

#define STRUN_LEAD 2600                 // main CPU cycles: 0.67 ms, about 4 samples
#ifndef STRUN_SKIP
#define STRUN_SKIP 4                    // a picture at least every 4 lists, sent or not
#endif

typedef struct {
    stcv_t *cv;
    uint32_t lists;                     // since the start
    uint64_t target;                    // the main CPU cycle this list runs to
    uint32_t pictures;
    uint32_t waited;                    // VBLANKs that made no picture (the last one's bytes not all sent)
    uint8_t since;                      // lists since the last picture
    int16_t pcm[STCV_PCM_N];
} strun_t;

// the board started (st_init done)
void strun_start(strun_t *r, stcv_t *cv);
// one list: the board for its time, then the list into out; returns its length
size_t strun_list(strun_t *r, uint8_t *out);
// st_on_vblank's own work (strun_start sets it; a test's hook calls it)
void strun_vblank(void);
