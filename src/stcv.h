// stcv: the cart's side of Star Trek on the ColecoVision. Portable C (no
// pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs the SDK's frame kernel (gbkernel.asm): each TV frame it
// takes a command list from the cart and runs it. This makes those lists
// from the board's vectors (st.h) and its sound (stsnd.h):
//
//   once:                          stcv_init(e)
//   at each picture the cart uses: stcv_vectors(e)       (or stcv_canvas for a message)
//   each list:                     stcv_sound_pcm(e, ...)
//                                  len = stcv_tv_frame(e, list)
//
// The picture. MAME draws the game's vectors on a screen 1,025 units wide
// and 833 high, shown at 4:3. The TMS9918's 256 x 192 is 4:3 too, so the
// whole screen is drawn on it, each vector a line a pixel wide in the
// colour nearest its own (stcv.c). At a quarter of MAME's size the
// scanner's ships (the top right) would be blobs of three or four pixels,
// so each small symbol there is drawn bigger about its own centre (the
// smallest 3.5 times, the rest twice): they stay where the game puts them
// and keep their shapes. The rest (the first-person view, the gauges, the
// words) is drawn as it is.
//
// The TMS9918 has two colours in each row of 8 pixels. Where vectors of
// different colours share one, the last drawn wins, except that the stars'
// grey gives way to any colour.
//
// A list carries the bytes that differ from what the console holds, as
// many as the frame has time for. The sound is samples through the
// SN76489's three tones parked at a period of 1, their attenuators summed
// as a DAC, STCV_PCM_N a list, with the console running the lists back to
// back (Donkey Kong's and Time Pilot's way).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "st.h"

#define STCV_W      256
#define STCV_H      192
#define STCV_BYTES  6144            // pattern bytes, and as many colour bytes
#define STCV_LIST_MAX 3000
#define STCV_PCM_N  100             // samples a list
#define STCV_LIST_T 60036           // a list's length in the console's T (3,579,545 a second)
#define STCV_CONSOLE_HZ 3579545u
// the board's main CPU cycles in a list, << 16
#define STCV_LIST_CYCLES_Q16 ((uint64_t)STCV_LIST_T * ST_CLOCK * 65536u / STCV_CONSOLE_HZ)

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define STCV_HOT __attribute__((section(".time_critical.stcv")))
#else
#define STCV_HOT
#endif

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // lists that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
    uint32_t clipped;               // samples the soft knee took in
    uint32_t lines;                 // vectors drawn
    uint32_t magnified;             // of them, in the scanner's symbols drawn twice their size
} stcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[STCV_BYTES], vram_col[STCV_BYTES];
    uint8_t want_pat[STCV_BYTES], want_col[STCV_BYTES];
    uint8_t want_pri[STCV_BYTES];       // each place's colour's rank while drawing (the stars' grey 0)
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                    // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                         // want differs from vram somewhere (or may)

    // the sound
    bool pcm;                           // the lists carry samples
    uint8_t pcm_gain;                   // 0-32: the DAC comes up over the first lists
    uint8_t pcm_bytes[STCV_PCM_N][3];   // each sample's three attenuators, in the order written
    uint8_t dac_att[3];                 // the attenuators as the last sample left them
    uint8_t tone_bytes[12];
    uint8_t tone_n;
    uint8_t resync;
    stcv_stats_t stats;
} stcv_t;

void stcv_init(stcv_t *e);

// a new picture: the vectors of the last VBLANK (st_segs)
void stcv_vectors(stcv_t *e, const st_seg_t *segs, int n);
// a new picture: 256 x 192 pixels, a TMS9918 colour each (1-15); for messages
void stcv_canvas(stcv_t *e, const uint8_t *px);
// room the plugin may use before the game (a message's canvas, the card's
// reads); stcv itself never touches it
uint8_t *stcv_scratch(size_t *n);

// The next list's sound: n samples in the DAC's levels either side of its
// rest, 64 a level (stsnd.h; n = 0: none, for messages: the console waits
// for the vertical blank again, and the DAC is let go).
void stcv_sound_pcm(stcv_t *e, const int16_t *s, int n);

// the next list into out (STCV_LIST_MAX bytes); returns its length
size_t stcv_tv_frame(stcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, a TMS9918
// colour a pixel (tests, previews); the picture wanted
void stcv_screen(const stcv_t *e, uint8_t *px);
void stcv_wanted(const stcv_t *e, uint8_t *px);
// bytes the console still needs (tests; a whole scan)
uint32_t stcv_backlog(const stcv_t *e);
// the DAC's level for the three tones' attenuators (tests: the lists' sound as a WAV)
float stcv_dac_level(int att0, int att1, int att2);
// the TMS9918's colours as 0xRRGGBB (previews)
extern const uint32_t stcv_tms_rgb[16];
// the TMS9918 colour each of the board's 64 (RGB, 2 bits each) is drawn in
extern uint8_t stcv_colour[64];
