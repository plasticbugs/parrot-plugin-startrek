// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// stsnd.c -- see stsnd.h. The synthesizer follows MAME 0.288's
// segausb.cpp (its non-netlist sound_stream_update), at a lower rate.
#include "stsnd.h"

#include <math.h>
#include <string.h>

#include "st.h"
#include "stcv.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define SND_HOT __attribute__((section(".time_critical.stsnd")))
#else
#define SND_HOT
#endif

// The USB's 2 MHz ticks a sample: STCV_PCM_N samples in a list of
// STCV_LIST_T of the console's T, kept as a whole part and a remainder so
// nothing drifts. A step is half a sample (STSND_OVER).
#define TICKS_NUM ((uint64_t)ST_USB_RATE * STCV_LIST_T)
#define TICKS_DEN ((uint64_t)STCV_PCM_N * STCV_CONSOLE_HZ * STSND_OVER)
static uint32_t step_q, step_r, step_rem;
static uint64_t tick;                   // the 2 MHz tick the next step starts at
uint32_t stsnd_samples;

// DAC levels for 1.0 of MAME's. MAME's own startrek is louder than an
// ordinary cartridge (-17.7 dBFS rms over the scripted game against about
// -22). 1400 puts 13% of the samples in the DAC's soft knee, as Time
// Pilot's 1900 puts 11% of its own (where its lists replay at -24.5 dBFS
// on MAME's ColecoVision), which makes these about -23.6 (test/st/
// soundcmp.sh's estimate: the replay needs the ColecoVision BIOS).
#ifndef GAIN_LEVELS
#define GAIN_LEVELS 1400.0f
#endif

// ---- the board's writes, in the order made ---------------------------------------------
#define WQ 512                          // (the scripted game never has more than 85 waiting)
static struct { uint64_t tick; int16_t reg; uint8_t value; } wq[WQ];
static uint32_t wq_in, wq_out;
#ifdef STSND_STATS
uint32_t stsnd_wq_max;
#endif

void st_usb_write(uint64_t t, int reg, uint8_t value) {
    if (wq_in - wq_out >= WQ) return;   // (can't happen: a list's writes are a few dozen)
    wq[wq_in % WQ].tick = t;
    wq[wq_in % WQ].reg = (int16_t)reg;
    wq[wq_in % WQ].value = value;
    wq_in++;
#ifdef STSND_STATS
    if (wq_in - wq_out > stsnd_wq_max) stsnd_wq_max = wq_in - wq_out;
#endif
}
void st_gain_write(uint64_t t, uint8_t control) {
    if (wq_in - wq_out >= WQ) return;
    wq[wq_in % WQ].tick = t;
    wq[wq_in % WQ].reg = -1;
    wq[wq_in % WQ].value = control;
    wq_in++;
}

// the SP0250's samples (10 kHz: 200 ticks each), as the board makes them
#define SPQ 4096
static int8_t spq[SPQ];
static uint32_t sp_made;
static uint32_t sp_i, sp_off;           // the sample the sound has reached, and its ticks into it
void st_sp_sample(uint32_t index, int8_t dac) {
    spq[index % SPQ] = dac;
    sp_made = index + 1;
}

// ---- the synthesizer ---------------------------------------------------------------------
typedef struct {
    uint8_t holding, latchmode, latchtoggle, clockmode, output;
    uint16_t count, remain;
    uint32_t left;                      // a square wave's PCS clocks to its next turn
} chan_t;

typedef struct {
    chan_t ch[3];
    float env[3];
    float cr[2];                        // the two square waves' CR filters (their capacitors)
    float g1, g2;                       // the switched RC filters
    uint8_t config;
} group_t;

static group_t grp[3];
static uint32_t noise_shift, noise_sub;
static float pink[3], noise_cr, final_cr;
static float gain_sp, gain_usb;
static float last_mame;                 // the last sample, for the tests

// Each filter's step at the step rate (MAME's are a 2 MHz tick's): a
// capacitor's exponent 1 - e^(-T/RC) for T the step's time. The pink-noise
// filters (c = k c + g x a tick) become c = k^N c + g (1 - k^N) / (1 - k) x
// for N ticks a step, the input held.
static float e_chan, e_noise, e_final;
static float pk[3], pg[3];
static float gate1_e[65], gate2_e[65];  // the switched filters' exponents by channel 2's time high (64ths)
static float ticks_step;

static float rc_exp(float r, float c) { return 1.0f - expf(-ticks_step / (r * c * (float)ST_USB_RATE)); }

void stsnd_init(void) {
    step_q = (uint32_t)(TICKS_NUM / TICKS_DEN);
    step_r = (uint32_t)(TICKS_NUM % TICKS_DEN);
    step_rem = 0;
    tick = 0;
    sp_i = sp_off = 0;
    stsnd_samples = 0;
    wq_in = wq_out = 0;
    sp_made = 0;
    memset(spq, 0, sizeof spq);
    memset(grp, 0, sizeof grp);
    noise_shift = 0x15555;
    noise_sub = 0;
    memset(pink, 0, sizeof pink);
    noise_cr = final_cr = 0;
    gain_sp = gain_usb = 1.0f;          // (the speech board's inputs start at 1.0)
    last_mame = 0;
    ticks_step = (float)((double)TICKS_NUM / (double)TICKS_DEN);
    e_chan = rc_exp(10e3f, 1e-6f);
    e_noise = rc_exp(33e3f, 0.1e-6f);
    e_final = rc_exp(100e3f, 4.7e-6f);
    static const float k[3] = {0.99765f, 0.96300f, 0.57000f}, g[3] = {0.0990460f, 0.2965164f, 1.0526913f};
    for (int i = 0; i < 3; i++) {
        pk[i] = powf(k[i], ticks_step);
        pg[i] = g[i] * (1.0f - pk[i]) / (1.0f - k[i]);
    }
    // the filters' exponents for channel 2's output high h/64 of the time:
    // low, their resistors are 100k and 200k, high 1k and 2k
    for (int h = 0; h <= 64; h++) {
        float high = (float)h / 64.0f;
        float l1 = high * -1.0f / (1e3f * 0.01e-6f * ST_USB_RATE) + (1.0f - high) * -1.0f / (100e3f * 0.01e-6f * ST_USB_RATE);
        float l2 = high * -1.0f / (2e3f * 0.01e-6f * ST_USB_RATE) + (1.0f - high) * -1.0f / (200e3f * 0.01e-6f * ST_USB_RATE);
        gate1_e[h] = 1.0f - expf(ticks_step * l1);
        gate2_e[h] = 1.0f - expf(ticks_step * l2);
    }
}

// segausb.cpp's timer_w and env_w
static void usb_write(int reg, uint8_t data) {
    group_t *g = &grp[(reg >> 3) % 3];
    int off = reg & 7;
    if (off < 3) {
        chan_t *ch = &g->ch[off];
        bool was_holding = ch->holding;
        switch (ch->latchmode) {
        case 1: ch->count = data; ch->holding = 0; break;
        case 2: ch->count = (uint16_t)(data << 8); ch->holding = 0; break;
        case 3:
            if (!ch->latchtoggle) { ch->count = (uint16_t)((ch->count & 0xff00) | data); ch->latchtoggle = 1; }
            else { ch->count = (uint16_t)((ch->count & 0x00ff) | data << 8); ch->holding = 0; ch->latchtoggle = 0; }
            break;
        }
        if (was_holding && !ch->holding) { ch->remain = 1; ch->left = 1; }
    } else if (off == 3) {
        if ((data >> 6) < 3) {
            chan_t *ch = &g->ch[data >> 6];
            ch->holding = 1;
            ch->latchmode = (data >> 4) & 3;
            ch->clockmode = (data >> 1) & 7;
            ch->latchtoggle = 0;
            ch->output = ch->clockmode == 1;
        }
    } else if (off < 7) {
        g->env[off - 4] = (float)data;
    } else {
        g->config = data & 1;
    }
}

// Channel 0 or 1 for n of its 1 MHz clocks: the clocks its output is high.
// Mode 3 turns every half count (count + 1 halved); mode 1 (one-shot) has
// had its trigger long since (the gate is held high from the start), so
// it only counts down to high; the other modes hold their output.
static SND_HOT uint32_t square(chan_t *c, uint32_t n) {
    if (c->holding || !n) return c->output ? n : 0;
    uint32_t high = 0;
    if (c->clockmode == 3) {
        uint32_t half = c->count ? (c->count + 1u) >> 1 : 32768u;
        while (n) {
            if (n < c->left) {
                if (c->output) high += n;
                c->left -= n;
                break;
            }
            if (c->output) high += c->left;
            n -= c->left;
            c->output ^= 1;
            c->left = half;
            if (n >= 2 * half) {            // whole periods: half of each high
                uint32_t k = n / (2 * half);
                high += k * half;
                n -= k * 2 * half;
            }
        }
        return high;
    }
    if (c->clockmode == 1) {
        if (!c->output) {
            uint32_t k = c->remain ? c->remain : 65536u;
            if (n < k) { c->remain = (uint16_t)(c->remain - n); return 0; }
            c->output = 1;
            c->remain = (uint16_t)(c->remain - n);
            return n - (k - 1);
        }
        c->remain = (uint16_t)(c->remain - n);
        return n;
    }
    return c->output ? n : 0;
}

// Channel 2's output high, in 64ths of the time. It is clocked at 2 MHz
// and triggered every 64 ticks (the gate's rise); in mode 1 it goes low
// for count ticks from each trigger (1-63: high the rest; 0, or 64 and
// more: never high again before the next trigger). Mode 3: half the time.
// Held, or another mode: its output as it is.
static inline int duty(const chan_t *c) {
    if (c->holding) return c->output ? 64 : 0;
    if (c->clockmode == 1) return c->count >= 1 && c->count < 64 ? 64 - c->count : 0;
    if (c->clockmode == 3) return 32;
    return c->output ? 64 : 0;
}

// one step of n ticks from tick t: the USB's output, MAME's units
static SND_HOT float usb_step(uint64_t t, uint32_t n) {
    // the PCS clocks (every other tick, at the even ones) in [t, t + n)
    uint32_t pcs = (uint32_t)(((t + n + 1) >> 1) - ((t + 1) >> 1));
    // the noise: the MM5837's bit, shifted every 20 ticks, its mean over the step
    uint32_t ones = 0, left = n;
    while (left) {
        if (!noise_sub) {
            noise_shift = (noise_shift << 1) | (((noise_shift >> 13) ^ (noise_shift >> 16)) & 1);
            noise_sub = 20;
        }
        uint32_t k = left < noise_sub ? left : noise_sub;
        if (noise_shift >> 16 & 1) ones += k;
        noise_sub -= k;
        left -= k;
    }
    float bit = (float)ones / (float)n;
    for (int i = 0; i < 3; i++) pink[i] = pk[i] * pink[i] + pg[i] * bit;
    float noise = pink[0] + pink[1] + pink[2] + bit * 0.1848f;
    float y = noise - noise_cr;
    noise_cr += y * e_noise;
    noise = y * 0.075f;

    float sample = 0;
    const float inv_pcs = pcs ? 1.0f / (float)pcs : 0.0f;
    for (int gi = 0; gi < 3; gi++) {
        group_t *g = &grp[gi];
        float c[2];
        for (int k = 0; k < 2; k++) {
            float x = (float)square(&g->ch[k], pcs) * inv_pcs;
            float v = x - g->cr[k];
            g->cr[k] += v * e_chan;
            c[k] = v * g->env[k] * (1.0f / 100.0f);
        }
        int d = duty(&g->ch[2]);
        float a1 = gate1_e[d], a2 = gate2_e[d], mix;
        if (g->config == 0) {
            g->g1 += (noise - g->g1) * a1;
            g->g2 += (g->g1 - g->g2) * a2;
            mix = c[0] + c[1] + g->g2 * -1.56f * g->env[2] * (1.0f / 33.0f);
        } else {
            float in = -(c[0] + c[1] - noise * g->env[2] * (1.0f / 33.0f));
            g->g1 += (in - g->g1) * a1;
            g->g2 += (g->g1 - g->g2) * a2;
            mix = g->g2 * 1.56f;
        }
        sample += mix;
    }
    return sample;
}

// the SP0250's mean over the next n ticks, in MAME's units (sp_i, sp_off:
// where the sound has reached)
static SND_HOT float speech(uint32_t n) {
    float sum = 0;
    for (uint32_t left = n; left;) {
        uint32_t k = 200 - sp_off;
        if (k > left) k = left;
        if ((int32_t)(sp_made - sp_i) > 0) sum += (float)spq[sp_i % SPQ] * (float)k;
        sp_off += k;
        left -= k;
        if (sp_off == 200) { sp_off = 0; sp_i++; }
    }
    return sum * (1.0f / 128.0f) / (float)n;
}

static SND_HOT void apply_writes(uint64_t until) {
    while (wq_out != wq_in && wq[wq_out % WQ].tick < until) {
        int reg = wq[wq_out % WQ].reg;
        uint8_t v = wq[wq_out % WQ].value;
        if (reg < 0) {
            gain_sp = (v & 0x08) ? 1.0f : 0.0f;
            gain_usb = (v & 0x20) ? 1.0f : 0.0f;
        } else {
            usb_write(reg, v);
        }
        wq_out++;
    }
}

static SND_HOT float next_sample(float *sp_out, float *usb_out) {
    const uint64_t a = tick;
    float usb = 0;
    for (int s = 0; s < STSND_OVER; s++) {
        uint32_t n = step_q;
        step_rem += step_r;
        if (step_rem >= TICKS_DEN) { step_rem -= (uint32_t)TICKS_DEN; n++; }
        apply_writes(tick + n / 2);     // (a write in the step's first half counts from its start)
        usb += usb_step(tick, n);
        tick += n;
    }
    usb *= 1.0f / STSND_OVER;
    float y = usb - final_cr;           // the final CR filter, then MAME's 0.1
    final_cr += y * (e_final * STSND_OVER);
    usb = 0.1f * y * gain_usb;
    float sp = speech((uint32_t)(tick - a)) * gain_sp;
    if (sp_out) *sp_out = sp;
    if (usb_out) *usb_out = usb;
    stsnd_samples++;
    return last_mame = sp + usb;
}

SND_HOT void stsnd_pcm(int16_t *out, int n) {
    for (int k = 0; k < n; k++) {
        float v = next_sample(NULL, NULL) * (GAIN_LEVELS * 64.0f);
        out[k] = (int16_t)(v > 32767.0f ? 32767 : v < -32767.0f ? -32767 : (int)v);
    }
}

void stsnd_mame(float *out, int n, float *sp, float *usb) {
    for (int k = 0; k < n; k++) {
        float a, b;
        float v = next_sample(&a, &b);
        if (out) out[k] = v;
        if (sp) sp[k] = a;
        if (usb) usb[k] = b;
    }
}
