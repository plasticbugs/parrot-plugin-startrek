// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// stsnd: the board's sound as the cart sends it, samples at the lists'
// rate (stcv.h: STCV_PCM_N a list, 5,962 a second). Portable.
//
// Two sources, mixed as the speech board mixes them (its control port's
// bits 3 and 5 turn each on):
//
//   the SP0250's speech: st.c makes its samples exactly as MAME 0.288 does
//   (10,000 a second); here each of ours is the mean of those it covers.
//
//   the Universal Sound Board: MAME 0.288 runs its synthesizer at 2 MHz
//   (segausb.cpp: three 8253s, each with two square waves, a noise source
//   through switched RC filters, and envelopes), far too slow for the cart.
//   Here it runs at STSND_OVER steps a sample, from the 8253s' and the
//   envelopes' writes (st_usb_write, with their 2 MHz ticks): each square
//   wave's time high in a step counted exactly (a box filter), the noise's
//   bits averaged the same way, and every RC filter turned into its step
//   at the lower rate. The switched filters' two time constants are mixed
//   by the share of time each is on (the duty the game sets).
#pragma once
#include <stdint.h>

#define STSND_OVER 2                    // synthesizer steps a sample

void stsnd_init(void);
// the next n samples, in the DAC's levels either side of its rest, 64 a
// level (tpcv's way: +-32767 is +-511 levels); the board must have run
// past their time (strun.c's lead)
void stsnd_pcm(int16_t *out, int n);
// the same samples in MAME's units (1.0 full scale), for the tests; NULL
// parts are skipped
void stsnd_mame(float *out, int n, float *speech, float *usb);
extern uint32_t stsnd_samples;          // samples made so far
