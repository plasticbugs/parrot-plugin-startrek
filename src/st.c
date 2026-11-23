// license:BSD-3-Clause
// copyright-holders:Aaron Giles, O. Galibert
//
// st.c -- the Star Trek board as MAME 0.288 emulates it, in C for the cart:
//
//   segag80v.cpp (Aaron Giles): the main CPU's maps and ports, the wait
//     states, the scrambled writes, the multiplier, the spinner, the coin
//     and VBLANK interrupts
//   segag80v_v.cpp (Aaron Giles): the vector generator's timing (how long
//     it draws, which the game reads back), and its vectors
//   segag80_m.cpp: the security chip 315-0064's scrambling
//   segausb.cpp (Aaron Giles): the Universal Sound Board's 8035, its
//     latches, its RAM and its T1 clock (its synthesizer is stsnd.c's)
//   segaspeech.cpp (Aaron Giles): the speech board's 8035 and its latch
//   sp0250.cpp (Olivier Galibert): the SP0250, whose DRQ the speech
//     board's 8035 waits on, so it runs here
//   schedule.cpp, diexec.cpp, screen.cpp (Aaron Giles), sound.cpp (O.
//     Galibert, Aaron Giles): MAME's timeslices (three CPUs, the 1/60 s
//     quantum and the USB's perfect quantum), timers, input line events,
//     a device held in reset eating its cycles, the screen's timers and
//     the streams' updates
//
// Times are MAME's attotimes as attoseconds from a whole second (epoch),
// moved on a second at a time between timeslices, as Time Pilot's tp.c
// (Parrot Cart's timeplt) has them; the scheduler is its, with a third
// device. MAME's license (BSD-3-Clause) is in LICENSE (THIRD-PARTY.md).
#include "st.h"

#include <string.h>

#include "st48.h"
#include "stz80.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define ST_HOT __attribute__((section(".time_critical.st")))
#else
#define ST_HOT
#endif

// ---------------------------------------------------------------------------
// time

typedef int64_t at_t;                   // attoseconds since the epoch (epoch_s seconds)
#define AS_PER_SEC 1000000000000000000LL
#define AT_NEVER INT64_MAX
static uint32_t epoch_s;

// floor(a * f / 1e18) for 0 <= a < 1e18, without 128-bit sums
static inline uint64_t mul_frac(int64_t a, uint32_t f) {
	uint64_t ah = (uint64_t)a / 1000000000u, al = (uint64_t)a % 1000000000u;
	uint64_t x = ah * f, y = al * f;
	uint64_t q1 = x / 1000000000u, r1 = x % 1000000000u;
	return q1 + (r1 * 1000000000u + y) / (uint64_t)AS_PER_SEC;
}

static uint64_t at_ticks(at_t t, uint32_t f) {
	uint64_t s = epoch_s;
	while (t >= AS_PER_SEC) { t -= AS_PER_SEC; s++; }
	return s * f + mul_frac(t, f);
}

// floor(x / d) for a constant d, without the library's 64-bit divide
// (slow on the M33): m = floor(2^64 / d) makes the high half of x * m
// floor(x / d) or one less
typedef struct { uint64_t d, m; } udiv_t;
static inline uint64_t mulhi64(uint64_t a, uint64_t b) {
	uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
	uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
	uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
	return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
static inline uint64_t udiv(uint64_t x, const udiv_t *k) {
	uint64_t q = mulhi64(x, k->m);
	if (x - q * k->d >= k->d) q++;
	return q;
}
#define UDIV(d) {(uint64_t)(d), (uint64_t)(~0ull / (uint64_t)(d))}

// at_ticks for a rate that divides a second's attoseconds (the SP0250's
// 10 kHz, the USB's 2 MHz): floor(t / (1e18 / f))
static inline uint64_t at_ticks_exact(at_t t, uint32_t f, const udiv_t *k) {
	uint64_t s = epoch_s;
	while (t >= AS_PER_SEC) { t -= AS_PER_SEC; s++; }
	return s * f + udiv((uint64_t)t, k);
}

// the screen: a vector screen, set_refresh_hz(40), 400 x 300, visible
// area (512, 1536, 608, 1440): VBLANK starts at line 1441, past its height,
// so the sums below (screen.cpp's) put it at line 0 and its end straight
// after its start
#define FRAME_AS (AS_PER_SEC / 40)
#define VTOTAL 300
#define HTOTAL 400
#define SCAN_AS (FRAME_AS / VTOTAL)
#define PIX_AS (FRAME_AS / (VTOTAL * HTOTAL))
#define VIS_BOTTOM 1440
#define QUANTUM_AS (AS_PER_SEC / 60)            // the 60 Hz quantum
#define PERFECT_AS (AS_PER_SEC / ST_USB_CPS)    // perfect_quantum: the second smallest minimum_quantum (the 8035's cycle)
#define PERFECT_FOR (250 * 1000000000000LL)     // attotime::from_usec(250)
#define SND50_AS (AS_PER_SEC / 50)              // the sound manager's update
#define T1_AS (AS_PER_SEC / 7812)               // the USB's T1 clock: from_hz(2 MHz / 256)
#define SP_AS (AS_PER_SEC / ST_SP_RATE)         // the SP0250's stream update (DRQ)
// the vector generator (segag80v_v.cpp)
#define VECTOR_CLOCK 15468480
#define U34_CLOCK (VECTOR_CLOCK / 3)
#define VCL_AS (AS_PER_SEC / (U34_CLOCK / 2))
#define U51_AS (AS_PER_SEC / (U34_CLOCK / 2 / 16))
#define IRQ_AS (AS_PER_SEC / (U34_CLOCK / 0x1f788))  // 40 Hz: a frame

static const st_roms_t *roms;
st_inputs_t st_in = {0xdf, 0xff, ST_D3D2, ST_D1D0, 0x00, 0};
static st_inputs_t in_cur = {0xdf, 0xff, ST_D3D2, ST_D1D0, 0x00, 0};
uint8_t st_ram[0x800], st_vram[0x1000], st_usb_pgm[0x1000], st_usb_work[0x400];
uint32_t st_frames;
void (*st_on_vblank)(void);
st_vstats_t st_vstats;
const uint8_t *st_sine;
const uint8_t *st_speech_rom;
void (*st_usb_trace)(void), (*st_speech_trace)(void);
st_seg_t st_segs[ST_SEGS];
uint16_t st_nsegs;
static uint8_t zero_page[0x100];        // unmapped reads: 0
static uint8_t sink[0x100];             // writes that go nowhere

// ---------------------------------------------------------------------------
// the scheduler (src/emu/schedule.cpp, src/emu/diexec.cpp)

enum { D_MAIN, D_USB, D_SP, D_N };      // MAME's device order: maincpu, usbsnd:ourcpu, speech:cpu
#define SUSPEND_REASON_RESET 0x0001

typedef struct {
	at_t local;                         // m_localtime
	int64_t aspc;                       // m_attoseconds_per_cycle
	int64_t lt_aspc;                    // cycles_to_attotime(1): clocks a cycle times attoseconds a clock
	uint32_t cps;                       // m_cycles_per_second
	int divshift;
	uint32_t divisor;
	uint32_t inv;                       // 2^62 / divisor, for div_const
	uint64_t total;                     // m_totalcycles
	uint32_t suspend, nextsuspend;
	bool eat, nexteat;                  // m_eatcycles, m_nexteatcycles
} exec_t;

static exec_t ex[D_N];
static uint8_t order[D_N];              // m_execute_list: the running ones, then the suspended, each in device order
static int executing = -1;
static bool suspend_changes_pending;
static at_t basetime;
static bool perfect_on;                 // the perfect quantum is in the list (ahead of the 60 Hz one)
static at_t perfect_expire;

static void exec_init(exec_t *e, uint32_t cps, uint32_t clock, uint32_t divider) {
	memset(e, 0, sizeof *e);
	e->cps = cps;
	e->aspc = AS_PER_SEC / cps;
	e->lt_aspc = (AS_PER_SEC / clock) * divider;
	int64_t attos = e->aspc;
	e->divshift = 0;
	while (attos >= (1LL << 31)) { e->divshift++; attos >>= 1; }
	e->divisor = (uint32_t)attos;
	e->inv = (uint32_t)((1ull << 62) / e->divisor);
}

// MAME's divu_64x32(delta >> divshift, divisor), without the library's
// 64-bit divide (Time Pilot's tp.c): the divisors here are over 2^30
static inline __attribute__((always_inline)) int div_const(uint64_t x, uint32_t d, uint32_t m) {
	if (__builtin_expect(x >> 40 != 0, 0)) return (int)(x / d);
	uint32_t xl = (uint32_t)x, xh = (uint32_t)(x >> 32);
	uint64_t t = (uint64_t)xh * m + (((uint64_t)xl * m) >> 32);
	uint32_t q = (uint32_t)(t >> 30);
	if (x - (uint64_t)q * d >= d) q++;
	return (int)q;
}

static inline int dev_done(int d) {
	switch (d) {
	case D_MAIN: return stz_done();
	case D_USB: return usb48_done();
	default: return sp48_done();
	}
}

static void dev_abort(int d) {
	switch (d) {
	case D_MAIN: stz_abort(); break;
	case D_USB: usb48_abort(); break;
	default: sp48_abort(); break;
	}
}

static void abort_timeslice(void) {     // device_scheduler::abort_timeslice: the executing device's
	if (executing >= 0) dev_abort(executing);
}

static void rebuild_execute_list(void) {
	int n = 0;
	for (int d = 0; d < D_N; d++) if (!ex[d].suspend) order[n++] = (uint8_t)d;
	for (int d = 0; d < D_N; d++) if (ex[d].suspend) order[n++] = (uint8_t)d;
}

static void apply_suspend_changes(void) {
	uint32_t changed = 0;
	for (int d = 0; d < D_N; d++) {
		changed |= ex[d].suspend ^ ex[d].nextsuspend;
		ex[d].suspend = ex[d].nextsuspend;
		ex[d].eat = ex[d].nexteat;
	}
	if (changed) rebuild_execute_list();   // (and the changes stay pending, as MAME's do, for one more look)
	else suspend_changes_pending = false;
}

static void suspend(int d, uint32_t reason, bool eat) {
	ex[d].nextsuspend |= reason;
	ex[d].nexteat = eat;
	suspend_changes_pending = true;
	if (executing == d) dev_abort(d);
}

static void resume(int d, uint32_t reason) {
	ex[d].nextsuspend &= ~reason;
	suspend_changes_pending = true;
	if (executing == d) dev_abort(d);
}

// machine().time(): the timer's time in a callback, the running device's while it runs, else the base
static bool in_callback;
static at_t cb_expire;
static inline at_t now(void) {
	if (in_callback) return cb_expire;
	if (executing >= 0) return ex[executing].local + (at_t)dev_done(executing) * ex[executing].lt_aspc;
	return basetime;
}
static inline bool time_is_zero(void) { return epoch_s == 0 && now() == 0; }

// perfect_quantum(250 us): add_scheduling_quantum(0, 250 us), the
// quantum's actual length the perfect interleave's
static void perfect_quantum(void) {
	at_t cur = now(), expire = cur + PERFECT_FOR;
	if (!perfect_on || cur >= perfect_expire) perfect_expire = expire;
	else if (expire > perfect_expire) perfect_expire = expire;
	perfect_on = true;
}

// ---- timers: a list in expiry order, ties first come first ---------------------------------
enum {
	TM_SND50,                           // the sound manager's streams update (made first)
	TM_VBSTART, TM_VBEND, TM_SCAN0,     // the screen's
	TM_SP,                              // the SP0250's stream update, 10 kHz (its DRQ)
	TM_T1,                              // the USB's T1 clock, 7812 Hz
	TM_SYNC,                            // temporary timers (synchronize()) from here
	TM_COUNT = TM_SYNC + 24
};
enum { SY_NONE, SY_INPUT, SY_SPEECH, SY_USB };
enum { IN_MAIN_IRQ, IN_USB_RESET, IN_SP_IRQ, IN_N };
#define CLEAR_LINE 0
#define ASSERT_LINE 1

typedef struct {
	at_t expire, period;
	int8_t next;                        // the next in the list, -1 at its end
	bool enabled, listed, used;
	uint8_t type;
	int param;
} tmr_t;

static tmr_t tm[TM_COUNT];
static int tm_head = -1;
static int cb_timer = -1;
static bool cb_modified;

static void tm_remove(int i) {
	if (!tm[i].listed) return;
	if (tm_head == i) tm_head = tm[i].next;
	else {
		int p = tm_head;
		while (tm[p].next != i) p = tm[p].next;
		tm[p].next = tm[i].next;
	}
	tm[i].listed = false;
}

// timer_list_insert: before the first that expires later (a disabled or never one isn't listed)
static void tm_insert(int i) {
	tmr_t *t = &tm[i];
	if (!t->enabled || t->expire == AT_NEVER) return;
	int prev = -1, cur = tm_head;
	while (cur >= 0 && tm[cur].expire <= t->expire) { prev = cur; cur = tm[cur].next; }
	t->next = (int8_t)cur;
	if (prev < 0) tm_head = i;
	else tm[prev].next = (int8_t)i;
	t->listed = true;
}

static inline at_t head_expire(void) { return tm_head >= 0 ? tm[tm_head].expire : AT_NEVER; }

static void tm_adjust(int i, at_t delay, int param, at_t period) {
	tmr_t *t = &tm[i];
	if (in_callback && cb_timer == i) cb_modified = true;
	t->param = param;
	t->enabled = true;
	t->used = true;
	if (delay < 0) delay = 0;
	t->expire = delay == AT_NEVER ? AT_NEVER : now() + delay;
	t->period = period;
	tm_remove(i);
	tm_insert(i);
	if (tm_head == i) abort_timeslice();
}

static void synchronize(int type, int param) {
	for (int i = TM_SYNC; i < TM_COUNT; i++) {
		if (tm[i].used) continue;
		tm[i].type = (uint8_t)type;
		tm[i].used = true;
		tm[i].param = param;
		tm[i].period = AT_NEVER;
		tm[i].expire = now();
		tm[i].enabled = true;
		tm_insert(i);
		if (tm_head == i) abort_timeslice();
		return;
	}
}

// ---- input lines (device_input::set_state_synced): a batch of events, applied by a timer now
static struct { uint8_t q[32]; int n; uint8_t cur; } inq[IN_N];
static void usb_cpu_reset(void);

static void input_apply(int line, int state) {
	inq[line].cur = (uint8_t)state;
	switch (line) {
	case IN_MAIN_IRQ: stz_set_irq(state != CLEAR_LINE); break;
	case IN_SP_IRQ: sp48_set_irq(state != CLEAR_LINE); break;
	case IN_USB_RESET:                  // RESET: held, the device eats its cycles; let go, it's reset
		if (state == ASSERT_LINE) suspend(D_USB, SUSPEND_REASON_RESET, true);
		else if (ex[D_USB].nextsuspend & SUSPEND_REASON_RESET) {
			usb_cpu_reset();
			resume(D_USB, SUSPEND_REASON_RESET);
		}
		break;
	}
}

static void empty_event_queue(int line) {
	for (int i = 0; i < inq[line].n; i++) input_apply(line, inq[line].q[i]);
	inq[line].n = 0;
}

static void set_input_line(int line, int state) {
	if (inq[line].n == (int)sizeof inq[line].q) {   // full: applied now (MAME logs it)
		inq[line].n--;
		empty_event_queue(line);
	}
	inq[line].q[inq[line].n++] = (uint8_t)state;
	if (inq[line].n == 1) synchronize(SY_INPUT, line);
}

// ---------------------------------------------------------------------------
// the screen's timers (screen.cpp)

static at_t vblank_start_time;
static bool frame_done;

// time_until_pos(v, 0), C's % and all (a negative line for line 0)
static at_t time_until_pos(int v) {
	v += VTOTAL - (VIS_BOTTOM + 1);
	v %= VTOTAL;
	int64_t target = (int64_t)v * SCAN_AS;
	int64_t cur = now() - vblank_start_time;
	if (target <= cur + PIX_AS / 2) target += FRAME_AS;
	while (target <= cur) target += FRAME_AS;
	return target - cur;
}

// ---------------------------------------------------------------------------
// the vector generator (segag80v_v.cpp's sega_generate_vector_list)

static at_t draw_end_time;              // m_draw_end_time

static inline bool clip1(unsigned u) { return u < 0x200 || u > 0x5ff; }   // adjust_xy's clipping, an axis
static inline int out1(unsigned u) { return u < 0x200 ? 0 : u > 0x5ff ? 0x3ff : (int)u - 0x200; }

// The steps k (0..n) at which an axis is on the screen, as [*lo, *hi]
// (empty: lo > hi). It starts at u (11 bits) and moves floor(k d / 256)
// after k steps (d: 0-256, the PROM's value plus its bit 7), up or down; a
// vector is 508 long at most, so it can't come round again.
static void axis_steps(unsigned u, int d, bool down, uint32_t n, uint32_t *lo, uint32_t *hi) {
	*lo = 1;
	*hi = 0;
	if (d == 0) {
		if (!clip1(u)) *lo = 0, *hi = n;
		return;
	}
	int64_t mtop = ((int64_t)n * d) >> 8, a, b;      // the moves: 0..mtop
	if (!down) {
		if (u > 0x5ff) return;
		a = u < 0x200 ? 0x200 - (int64_t)u : 0;
		b = 0x5ff - (int64_t)u;
	} else {
		if (u < 0x200) return;
		a = u > 0x5ff ? (int64_t)u - 0x5ff : 0;
		b = (int64_t)u - 0x200;
	}
	if (b > mtop) b = mtop;
	if (a > b) return;
	// (a and b are under 2^11: 32-bit divides)
	*lo = ((uint32_t)a * 256 + (uint32_t)d - 1) / (uint32_t)d;     // the first step with a move of a
	*hi = b == mtop ? n : (((uint32_t)b + 1) * 256 + (uint32_t)d - 1) / (uint32_t)d - 1;
}

static ST_HOT void vectors_frame(void) {
	int64_t tr = IRQ_AS;                // time_remaining
	const uint8_t *v = st_vram, *sine = st_sine;
	uint16_t sa = 0;
	st_vstats_t vs = {0, 0, 0, false};
	int ns = 0;
	while (tr > 0) {
		uint8_t draw = v[sa++ & 0xfff];
		unsigned x = v[sa++ & 0xfff];
		x |= (v[sa++ & 0xfff] & 7u) << 8;
		unsigned y = v[sa++ & 0xfff];
		y |= (v[sa++ & 0xfff] & 7u) << 8;
		unsigned va = v[sa++ & 0xfff];
		va |= (v[sa++ & 0xfff] & 0xfu) << 8;
		unsigned symangle = v[sa++ & 0xfff];
		symangle |= (v[sa++ & 0xfff] & 3u) << 8;
		unsigned scale = v[sa++ & 0xfff];
		tr -= 10 * U51_AS;
		vs.symbols++;
		if (draw & 1) {
			const int16_t ox = (int16_t)((int)(x & 0x7ff) - 0x200), oy = (int16_t)((int)(y & 0x7ff) - 0x200 - 96);
			// (only the 11 bits adjust_xy looks at are kept: the high byte's bit 2 copied to bit 3 is past them)
			while (tr > 0) {
				uint8_t attrib = v[va++ & 0xfff];
				uint32_t length = (uint32_t)(v[va++ & 0xfff] * scale) >> 7;
				unsigned vecangle = v[va++ & 0xfff];
				vecangle |= (v[va++ & 0xfff] & 3u) << 8;
				unsigned ang = vecangle + symangle;
				int dx = sine[(ang & 0x1ff) << 1], dy = sine[((ang + 0x100) & 0x1ff) << 1];
				dx += dx >> 7;
				dy += dy >> 7;
				bool xdown = (ang & 0x200) != 0, ydown = ((ang + 0x100) & 0x200) != 0;
				tr -= 4 * U51_AS;
				vs.vectors++;
				uint32_t n = 0;
				if (tr > 0) {
					static const udiv_t vcl_div = UDIV(VCL_AS);
					int64_t k = (int64_t)udiv((uint64_t)(tr + VCL_AS - 1), &vcl_div);
					n = (int64_t)length < k ? length : (uint32_t)k;
				}
				tr -= (int64_t)n * VCL_AS;
				vs.steps += n;
				unsigned mx = (unsigned)(((uint64_t)n * dx) >> 8), my = (unsigned)(((uint64_t)n * dy) >> 8);
				unsigned x1 = (xdown ? x - mx : x + mx) & 0x7ff, y1 = (ydown ? y - my : y + my) & 0x7ff;
				// a line where the beam is on and the colour isn't black, over the steps on the screen
				unsigned colour = (attrib >> 1) & 0x3f;
				if ((attrib & 1) && colour && ns < ST_SEGS) {
					uint32_t xl, xh, yl, yh;
					axis_steps(x & 0x7ff, dx, xdown, n, &xl, &xh);
					axis_steps(y & 0x7ff, dy, ydown, n, &yl, &yh);
					uint32_t lo = xl > yl ? xl : yl, hi = xh < yh ? xh : yh;
					if (lo <= hi) {
						// from where it comes on (k = lo), to the end or to the clipped point after it goes off
						uint32_t k1 = hi < n ? hi + 1 : n;
						unsigned ax = (unsigned)(((uint64_t)lo * dx) >> 8), ay = (unsigned)(((uint64_t)lo * dy) >> 8);
						unsigned bx = (unsigned)(((uint64_t)k1 * dx) >> 8), by = (unsigned)(((uint64_t)k1 * dy) >> 8);
						unsigned x0p = (xdown ? x - ax : x + ax) & 0x7ff, y0p = (ydown ? y - ay : y + ay) & 0x7ff;
						unsigned x1p = (xdown ? x - bx : x + bx) & 0x7ff, y1p = (ydown ? y - by : y + by) & 0x7ff;
						st_seg_t *s = &st_segs[ns++];
						s->x0 = (int16_t)out1(x0p);
						s->y0 = (int16_t)(out1(y0p) - 96);
						s->x1 = (int16_t)out1(x1p);
						s->y1 = (int16_t)(out1(y1p) - 96);
						s->ox = ox;
						s->oy = oy;
						s->colour = (uint8_t)colour;
						s->symbol = (uint8_t)vs.symbols;
					}
				}
				x = x1;
				y = y1;
				if (attrib & 0x80) break;
			}
		}
		if (draw & 0x80) break;
	}
	vs.out_of_time = tr <= 0;
	st_nsegs = (uint16_t)ns;
	st_vstats = vs;
	draw_end_time = now() + (IRQ_AS - tr);
}

static inline int draw_r(void) { return now() < draw_end_time; }

// ---------------------------------------------------------------------------
// the main board (segag80v.cpp)

static uint8_t mult_data[2];
static uint16_t mult_result;
static uint8_t spinner_select, spinner_sign, spinner_count;
static uint8_t coin_ff, coin_last, edgint_ff;

static void update_int(void) {
	set_input_line(IN_MAIN_IRQ, (coin_ff || edgint_ff) ? ASSERT_LINE : CLEAR_LINE);
	coin_ff &= (uint8_t)~0x04;          // SERVICE has no flip-flop
}

static void coin_w(int index, int state) {
	uint8_t mask = (uint8_t)(1u << index);
	if (state == 0 && (coin_last & mask)) coin_ff |= mask;
	else coin_ff &= (uint8_t)~mask;
	if (state) coin_last |= mask;
	else coin_last &= (uint8_t)~mask;
	update_int();
}

// the CPU's acknowledge: irqack_cb (INTCL clears the coins' and EDGINT's
// flip-flops), then the vector (the line is never held)
uint8_t stz_hook_irq_ack(void) {
	coin_ff = 0;
	edgint_ff = 0;
	update_int();
	return 0xff;
}

// ioport_manager::frame_update: the ports' new values (st_in), and the
// coins' write handlers where a bit changed (the ports in tag order: D5D4's
// SERVICE1, then D7D6's COIN1 and COIN2)
static void inputs_frame(void) {
	st_inputs_t old = in_cur;
	in_cur = st_in;
	if ((old.d5d4 ^ in_cur.d5d4) & 0x01) coin_w(2, in_cur.d5d4 & 1);
	if ((old.d7d6 ^ in_cur.d7d6) & 0x01) coin_w(0, in_cur.d7d6 & 1);
	if ((old.d7d6 ^ in_cur.d7d6) & 0x10) coin_w(1, (in_cur.d7d6 >> 4) & 1);
}

static uint8_t mangled_ports_r(int offset) {
	uint8_t d7d6 = (uint8_t)((in_cur.d7d6 & ~0x20) | draw_r() << 5);
	uint8_t d5d4 = in_cur.d5d4, d3d2 = in_cur.d3d2, d1d0 = in_cur.d1d0;
	return (uint8_t)(((d7d6 >> offset) & 1) << 7 | ((d7d6 >> (offset + 4)) & 1) << 6 |
	                 ((d5d4 >> offset) & 1) << 5 | ((d5d4 >> (offset + 4)) & 1) << 4 |
	                 ((d3d2 >> offset) & 1) << 3 | ((d3d2 >> (offset + 4)) & 1) << 2 |
	                 ((d1d0 >> offset) & 1) << 1 | ((d1d0 >> (offset + 4)) & 1));
}

static uint8_t spinner_input_r(void) {
	if (spinner_select & 1) return in_cur.fc;
	int8_t delta = in_cur.dial;         // (PORT_RESET: the frame's move, each read)
	if (delta != 0) {
		spinner_sign = (uint8_t)(((uint8_t)delta >> 7) & 1);
		spinner_count = (uint8_t)(spinner_count + (delta < 0 ? -delta : delta));
	}
	return (uint8_t)~((spinner_count << 1) | spinner_sign);
}

// the security chip 315-0064 (segag80_m.cpp's sega_decrypt64)
static uint8_t decrypt64(uint16_t pc, uint8_t lo) {
	uint32_t i = 0, b = lo;
	switch (pc & 3) {
	case 0: i = b; break;
	case 1:
		i = b & 0x03;
		i += (b & 0x80) >> 1;
		i += (b & 0x60) >> 3;
		i += (~b) & 0x10;
		i += (b & 0x08) << 2;
		i += (b & 0x04) << 5;
		i &= 0xff;
		break;
	case 2:
		i = b & 0x03;
		i += (b & 0x80) >> 4;
		i += ((~b) & 0x40) >> 1;
		i += (b & 0x20) >> 1;
		i += (b & 0x10) >> 2;
		i += (b & 0x08) << 3;
		i += (b & 0x04) << 5;
		i &= 0xff;
		break;
	default:
		i = b & 0x23;
		i += (b & 0xc0) >> 4;
		i += (b & 0x10) << 2;
		i += (b & 0x08) << 1;
		i += ((~b) & 0x04) << 5;
		i &= 0xff;
		break;
	}
	return (uint8_t)i;
}

// decrypt_offset: an LD (nn),A's write has its address's low byte scrambled
static uint16_t decrypt_offset(uint16_t a) {
	if (z80_scr_pc == 0xffff) return a;
	uint16_t pc = z80_scr_pc;
	z80_scr_pc = 0xffff;
	return (uint16_t)((a & 0xff00) | decrypt64(pc, (uint8_t)a));
}

// ---------------------------------------------------------------------------
// the Universal Sound Board (segausb.cpp), without its synthesizer

static uint8_t in_latch, out_latch, last_p2, work_bank, t1_clock;
#define T1_MASK 0x10

static const udiv_t usb_tick_div = UDIV(AS_PER_SEC / ST_USB_RATE);
uint64_t st_usb_now(void) { return time_is_zero() ? 0 : at_ticks_exact(now(), ST_USB_RATE, &usb_tick_div) + 1; }

static uint8_t usb_status_r(void) {
	stz_adjust_icount(-200);
	return (uint8_t)((out_latch & 0x81) | (in_latch & 0x7e));
}

static void usb_ram_w(uint16_t offset, uint8_t d) {
	if (in_latch & 0x80) st_usb_pgm[offset] = d;
}

uint8_t st_usb_p1_r(void) { return in_latch & 0x7f; }
void st_usb_p1_w(uint8_t d) { out_latch = (uint8_t)((out_latch & 0xfe) | (d >> 7)); }
void st_usb_p2_w(uint8_t d) {
	uint8_t old = last_p2;
	last_p2 = d;
	work_bank = d & 3;
	out_latch = (uint8_t)(((d & 0x40) << 1) | (out_latch & 0x7f));
	if (!(d & 0x40)) in_latch = 0;
	if ((old & 0x80) && !(d & 0x80)) t1_clock = 0;
}
int st_usb_t1(void) { return (t1_clock & T1_MASK) != 0; }
uint8_t st_usb_work_r(uint8_t a) { return st_usb_work[a + 256 * work_bank]; }
void st_usb_work_w(uint8_t a, uint8_t d) {
	unsigned off = a + 256u * work_bank;
	st_usb_work[off] = d;
	if (off < 0x18) st_usb_write(st_usb_now(), (int)off, d);   // the 8253s and the envelopes
}

static void usb_cpu_reset(void) {       // device_t::reset on the 8035: total cycles 0, resumed, its reset
	ex[D_USB].total = 0;
	ex[D_USB].nextsuspend = 0;
	suspend_changes_pending = true;
	usb48_reset();
}

// ---------------------------------------------------------------------------
// the speech board (segaspeech.cpp) and its SP0250 (sp0250.cpp)

static uint8_t sp_latch, sp_t0, sp_p2;
static bool sp_drq;

uint8_t st_sp_p1_r(void) { return sp_latch & 0x7f; }
void st_sp_p1_w(uint8_t d) { if (!(d & 0x80)) sp_t0 = 0; }
void st_sp_p2_w(uint8_t d) { sp_p2 = d; }
int st_sp_t0(void) { return sp_t0; }
int st_sp_t1(void) { return sp_drq; }
// (MAME's region is 0x4000 bytes, the two ROMs in its first 0x2000 and
// zeros after them)
uint8_t st_sp_rom_r(uint8_t a) {
	const unsigned at = 0x100u * (sp_p2 & 0x3f) + a;
	return at < 0x2000 ? roms->speech_data[at] : 0;
}

static struct {
	int16_t F[6], B[6], z1[6], z2[6];
	bool voiced;
	int16_t amp;
	uint16_t lfsr;
	uint8_t pitch, pcount, repeat, rcount;
	uint8_t fifo[15], fifo_pos;
	uint64_t written;                   // samples made (the stream's write_sample)
} sp;

static int16_t sp_gc(uint8_t v) {
	static const uint16_t coefs[128] = {
		0, 9, 17, 25, 33, 41, 49, 57, 65, 73, 81, 89, 97, 105, 113, 121,
		129, 137, 145, 153, 161, 169, 177, 185, 193, 201, 209, 217, 225, 233, 241, 249,
		257, 265, 273, 281, 289, 297, 301, 305, 309, 313, 317, 321, 325, 329, 333, 337,
		341, 345, 349, 353, 357, 361, 365, 369, 373, 377, 381, 385, 389, 393, 397, 401,
		405, 409, 413, 417, 421, 425, 427, 429, 431, 433, 435, 437, 439, 441, 443, 445,
		447, 449, 451, 453, 455, 457, 459, 461, 463, 465, 467, 469, 471, 473, 475, 477,
		479, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
		496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511};
	int16_t r = (int16_t)coefs[v & 0x7f];
	return (v & 0x80) ? r : (int16_t)-r;
}

static void sp_load_values(void) {
	static const uint8_t bf[6][2] = {{0, 1}, {3, 4}, {6, 7}, {9, 10}, {11, 12}, {13, 14}};
	for (int f = 0; f < 6; f++) {
		sp.B[f] = sp_gc(sp.fifo[bf[f][0]]);
		sp.F[f] = sp_gc(sp.fifo[bf[f][1]]);
		sp.z1[f] = sp.z2[f] = 0;
	}
	sp.amp = (int16_t)((sp.fifo[2] & 0x1f) << (sp.fifo[2] >> 5));
	sp.pitch = sp.fifo[5];
	sp.repeat = sp.fifo[8] & 0x3f;
	sp.voiced = (sp.fifo[8] & 0x40) != 0;
	sp.fifo_pos = 0;
	sp_drq = true;
	sp.pcount = 0;
	sp.rcount = 0;
}

static ST_HOT int8_t sp_next(void) {
	if (sp.rcount >= sp.repeat) {
		if (sp.fifo_pos == 15) sp_load_values();
		else {
			sp.repeat = 1;
			sp.pcount = 0;
			sp.rcount = 0;
		}
	}
	sp.lfsr = (uint16_t)(sp.lfsr ^ ((sp.lfsr ^ (sp.lfsr >> 1)) << 15));
	sp.lfsr >>= 1;
	int16_t z0;
	if (sp.voiced) z0 = sp.pcount == 0 ? sp.amp : 0;
	else z0 = (sp.lfsr & 1) ? sp.amp : (int16_t)-sp.amp;
	for (int f = 0; f < 6; f++) {
		int16_t o = (int16_t)(z0 + ((sp.z1[f] * sp.F[f]) >> 8) + ((sp.z2[f] * sp.B[f]) >> 9));
		sp.z2[f] = sp.z1[f];
		sp.z1[f] = o;
		z0 = o;
	}
	int dac = z0 >> 6;
	if (dac < -64) dac = -64;
	if (dac > 63) dac = 63;
	if (sp.pcount++ == sp.pitch) {
		sp.pcount = 0;
		sp.rcount++;
	}
	return (int8_t)dac;
}

// sound_stream::update: the samples to now's, including it (none at time 0)
static const udiv_t sp_tick_div = UDIV(AS_PER_SEC / ST_SP_RATE);
static ST_HOT void sp_update(void) {
	if (time_is_zero()) return;
	uint64_t idx = at_ticks_exact(now(), ST_SP_RATE, &sp_tick_div);
	while (sp.written <= idx) {
		st_sp_sample((uint32_t)sp.written, sp_next());
		sp.written++;
	}
}

void st_sp_write(uint8_t a, uint8_t d) {
	(void)a;
	sp_update();
	if (sp.fifo_pos != 15) {
		sp.fifo[sp.fifo_pos++] = d;
		if (sp.fifo_pos == 15) sp_drq = false;
	}
}

static uint8_t sp_gains = 0x28;         // the mixer's input gains, as control_w's bits 3 and 5 (both on at first)

static void speech_control_w(uint8_t d) {
	if ((d ^ sp_gains) & 0x28) {        // set_input_gain: the streams brought up to now first, if it changes
		sp_update();
		sp_gains = d & 0x28;
		st_gain_write(st_usb_now(), sp_gains);
	}
}

// ---------------------------------------------------------------------------
// the main CPU's memory and ports

void stz_hook_wr(uint16_t a, uint8_t d) {
	switch (a >> 12) {
	case 0xc:
		if (a >= 0xc800) st_ram[decrypt_offset((uint16_t)(a - 0xc800)) & 0x7ff] = d;
		return;
	case 0xd: usb_ram_w(decrypt_offset((uint16_t)(a - 0xd000)) & 0xfff, d); return;
	case 0xe: st_vram[decrypt_offset((uint16_t)(a - 0xe000)) & 0xfff] = d; return;
	default: return;                    // the ROMs, unmapped: nowhere
	}
}

uint8_t stz_hook_rd(uint16_t a) { return st_peek(a); }     // (every page is a page)

uint8_t stz_hook_in(uint16_t port) {
	switch (port & 0xff) {
	case 0x3f: return usb_status_r();
	case 0xbe: {
		uint8_t r = (uint8_t)mult_result;
		mult_result >>= 8;
		return r;
	}
	case 0xf8: case 0xf9: case 0xfa: case 0xfb: return mangled_ports_r(port & 3);
	case 0xfc: return spinner_input_r();
	default: return 0;                  // unmapped
	}
}

void stz_hook_out(uint16_t port, uint8_t d) {
	switch (port & 0xff) {
	case 0x38: synchronize(SY_SPEECH, d); return;                       // the speech board's data_w
	case 0x3b: speech_control_w(d); return;
	case 0x3f: synchronize(SY_USB, d); perfect_quantum(); return;        // the USB's data_w
	case 0xbd: mult_data[0] = d; return;
	case 0xbe: mult_data[1] = d; mult_result = (uint16_t)(mult_data[0] * mult_data[1]); return;
	case 0xf8: spinner_select = d; return;
	default: return;                    // unknown_w, the coin counters, unmapped
	}
}

static void map_pages(void) {
	for (int p = 0; p < 256; p++) {
		stz_rd_page[p] = zero_page;
		stz_wr_page[p] = sink;
		if (p < 0xc0) stz_rd_page[p] = roms->maincpu + (p << 8);
		else if (p >= 0xc8 && p < 0xd0) stz_rd_page[p] = stz_wr_page[p] = st_ram + ((p - 0xc8) << 8);
		else if (p >= 0xd0 && p < 0xe0) stz_rd_page[p] = st_usb_pgm + ((p - 0xd0) << 8), stz_wr_page[p] = 0;
		else if (p >= 0xe0 && p < 0xf0) stz_rd_page[p] = stz_wr_page[p] = st_vram + ((p - 0xe0) << 8);
	}
}

uint8_t st_peek(uint16_t a) { return stz_rd_page[a >> 8][a & 0xff]; }

// ---------------------------------------------------------------------------
// the timers' callbacks, and the timeslices

static void cb_vbstart(void) {
	vblank_start_time = now();
	// video().frame_update(): the vector list from the RAM as it is now,
	// then the inputs (Lua's frame_done comes between)
	st_frames++;
	frame_done = true;
	vectors_frame();
	if (st_on_vblank) st_on_vblank();
	inputs_frame();
	tm_adjust(TM_VBSTART, time_until_pos(VIS_BOTTOM + 1), 0, AT_NEVER);
	tm_adjust(TM_VBEND, 0, 0, AT_NEVER);           // time_until_vblank_end(): past already, so now
}

static void cb_sync(int type, int param) {
	switch (type) {
	case SY_INPUT: empty_event_queue(param); break;
	case SY_SPEECH: {                   // delayed_speech_w
		uint8_t old = sp_latch;
		sp_latch = (uint8_t)param;
		set_input_line(IN_SP_IRQ, (param & 0x80) ? CLEAR_LINE : ASSERT_LINE);
		if (!(old & 0x80) && (param & 0x80)) sp_t0 = 1;
		break;
	}
	case SY_USB: {                      // delayed_usb_data_w
		int d = param;
		set_input_line(IN_USB_RESET, (d & 0x80) ? ASSERT_LINE : CLEAR_LINE);
		if (!(last_p2 & 0x40)) d &= ~0x7f;
		in_latch = (uint8_t)d;
		break;
	}
	default: break;
	}
}

static ST_HOT void execute_timers(void) {
	while (tm_head >= 0 && tm[tm_head].expire <= basetime) {
		int h = tm_head;
		tmr_t *t = &tm[h];
		bool was_enabled = t->enabled;
		if (t->period == 0 || t->period == AT_NEVER) t->enabled = false;
		in_callback = true;
		cb_timer = h;
		cb_expire = t->expire;
		cb_modified = false;
		if (was_enabled) {
			switch (h) {
			case TM_SND50: sp_update(); break;                  // the streams' update
			case TM_VBSTART: cb_vbstart(); break;
			case TM_VBEND:                                      // vblank_callback(false): EDGINT
				edgint_ff = 1;
				update_int();
				break;
			case TM_SCAN0: tm_adjust(TM_SCAN0, time_until_pos(0), 0, AT_NEVER); break;
			case TM_SP: sp_update(); break;
			case TM_T1: if (!(last_p2 & 0x80)) t1_clock++; break;
			default: cb_sync(t->type, t->param); break;
			}
		}
		if (!cb_modified) {
			if (h >= TM_SYNC) {
				tm_remove(h);
				t->used = false;
			} else {                            // schedule_next_period
				t->expire = t->period == AT_NEVER ? AT_NEVER : t->expire + t->period;
				tm_remove(h);
				tm_insert(h);
			}
		}
		in_callback = false;
		cb_timer = -1;
	}
}

static void rebase(void) {             // a second on: every time less 1e18
	epoch_s++;
	basetime -= AS_PER_SEC;
	for (int d = 0; d < D_N; d++) ex[d].local -= AS_PER_SEC;
	for (int i = 0; i < TM_COUNT; i++) if (tm[i].used && tm[i].expire != AT_NEVER) tm[i].expire -= AS_PER_SEC;
	vblank_start_time -= AS_PER_SEC;
	draw_end_time -= AS_PER_SEC;
	if (perfect_on) perfect_expire -= AS_PER_SEC;
}

static inline int dev_run(int d, int cycles) {
	switch (d) {
	case D_MAIN: return stz_run(cycles);
	case D_USB: return usb48_run(cycles);
	default: return sp48_run(cycles);
	}
}

// one device's part of a slice (the loop in device_scheduler::timeslice):
// one held in reset with its cycles eaten has them counted, not run
static inline __attribute__((always_inline)) void slice_dev(int d, at_t *target) {
	exec_t *e = &ex[d];
	if (e->suspend && !e->eat) return;
	int64_t delta = *target - e->local;
	if (delta < e->aspc) return;
	int ran = div_const((uint64_t)delta >> e->divshift, e->divisor, e->inv);
	if (!e->suspend) {
		executing = d;
		ran = dev_run(d, ran);
		executing = -1;
	}
	e->total += (uint64_t)ran;
	e->local += (int64_t)ran * e->aspc;
	if (e->local < *target) *target = e->local > basetime ? e->local : basetime;
}

static ST_HOT void timeslice(void) {
	if (basetime >= AS_PER_SEC) rebase();
	if (perfect_on && basetime >= perfect_expire) perfect_on = false;    // an expired quantum goes
	at_t he;
	while (basetime < (he = head_expire())) {
		at_t target = basetime + (perfect_on ? PERFECT_AS : QUANTUM_AS);
		if (he < target) target = he;
		if (suspend_changes_pending) apply_suspend_changes();
		for (int i = 0; i < D_N; i++) slice_dev(order[i], &target);
		basetime = target;
	}
	execute_timers();
}

// ---------------------------------------------------------------------------

uint64_t st_cycles(void) { return ex[D_MAIN].total + (executing == D_MAIN ? (uint64_t)stz_done() : 0); }
uint64_t st_usb_cycles(void) { return ex[D_USB].total + (executing == D_USB ? (uint64_t)usb48_done() : 0); }
uint64_t st_speech_cycles(void) { return ex[D_SP].total + (executing == D_SP ? (uint64_t)sp48_done() : 0); }
uint64_t st_time(void) { return at_ticks(basetime, ST_CLOCK); }
void st_usb_regs(st48_regs_t *r) { usb48_regs(r); }
void st_speech_regs(st48_regs_t *r) { sp48_regs(r); }

void st_misc(st_misc_t *m) {
	m->in_latch = in_latch;
	m->out_latch = out_latch;
	m->last_p2 = last_p2;
	m->t1_clock = t1_clock;
	m->sp_latch = sp_latch;
	m->sp_t0 = sp_t0;
	m->sp_p2 = sp_p2;
	m->sp_drq = sp_drq;
	m->sp_fifo_pos = sp.fifo_pos;
	m->coin_ff = coin_ff;
	m->edgint_ff = edgint_ff;
	m->spinner_count = spinner_count;
	m->spinner_select = spinner_select;
	m->usb_in_reset = (ex[D_USB].nextsuspend & SUSPEND_REASON_RESET) != 0;
}

static void trace_hooks(void) {
	usb48_trace = st_usb_trace;
	sp48_trace = st_speech_trace;
}

void st_init(const st_roms_t *r) {
	roms = r;
	st_sine = r->sine;
	st_speech_rom = r->speech_cpu;
	memset(tm, 0, sizeof tm);
	tm_head = -1;
	epoch_s = 0;
	basetime = 0;
	executing = -1;
	in_callback = false;
	cb_timer = -1;
	perfect_on = false;
	exec_init(&ex[D_MAIN], ST_CLOCK, ST_CLOCK, 1);
	exec_init(&ex[D_USB], ST_USB_CPS, 6000000, 15);
	exec_init(&ex[D_SP], ST_SPEECH_CPS, 3120000, 15);
	for (int d = 0; d < D_N; d++) ex[d].suspend = SUSPEND_REASON_RESET;     // interface_pre_start
	rebuild_execute_list();
	memset(st_ram, 0, sizeof st_ram);
	memset(st_vram, 0, sizeof st_vram);
	memset(st_usb_pgm, 0, sizeof st_usb_pgm);
	memset(st_usb_work, 0, sizeof st_usb_work);
	memset(inq, 0, sizeof inq);
	memset(&sp, 0, sizeof sp);
	sp.lfsr = 0x7fff;
	in_cur = st_in;
	st_frames = 0;
	st_nsegs = 0;
	vblank_start_time = 0;
	draw_end_time = 0;
	mult_data[0] = mult_data[1] = 0;
	mult_result = 0;
	spinner_select = spinner_sign = spinner_count = 0;
	coin_ff = coin_last = edgint_ff = 0;
	in_latch = out_latch = last_p2 = work_bank = t1_clock = 0;
	sp_latch = sp_t0 = sp_p2 = 0;
	sp_gains = 0x28;
	z80_scr_pc = 0xffff;
	map_pages();
	trace_hooks();

	// the sound manager's 50 Hz timer, made before the devices start
	tm_adjust(TM_SND50, SND50_AS, 0, SND50_AS);
	// device_start, in MAME's order: the CPU, the screen's timers, the
	// 8035s, the SP0250 (its DRQ up, its timer at its frame rate)
	stz_start();
	tm_adjust(TM_VBSTART, time_until_pos(VIS_BOTTOM + 1), 0, AT_NEVER);
	tm_adjust(TM_SCAN0, time_until_pos(0), 0, AT_NEVER);
	usb48_start();
	sp48_start();
	sp_drq = true;
	tm_adjust(TM_SP, SP_AS, 0, SP_AS);

	// device_reset, in MAME's order (each device's own, then its children's)
	for (int d = 0; d < D_N; d++) {     // interface_pre_reset: total cycles to 0, resumed
		ex[d].total = 0;
		ex[d].nextsuspend = 0;
		suspend_changes_pending = true;
	}
	stz_reset();
	set_input_line(IN_USB_RESET, ASSERT_LINE);     // the USB holds its CPU in reset
	usb48_reset();
	tm_adjust(TM_T1, 0, 0, T1_AS);                 // its T1 clock's timer, from now
	sp48_reset();
	sp_load_values();                              // the SP0250's reset
}

// Run while st_time() < until: until's cycle as the first time it is
// reached, ceil(until * 1e18 / ST_CLOCK) attoseconds, in seconds and the
// rest (Time Pilot's tp_run_to)
uint32_t st_run_to(uint64_t until) {
	uint32_t f0 = st_frames;
	const uint64_t q = (uint64_t)AS_PER_SEC / ST_CLOCK, r = (uint64_t)AS_PER_SEC % ST_CLOCK;
	const uint64_t sec = until / ST_CLOCK, rem = until % ST_CLOCK;
	const at_t thr = (at_t)(rem * q + (rem * r + ST_CLOCK - 1) / ST_CLOCK);
	for (;;) {
		int64_t ds = (int64_t)sec - (int64_t)epoch_s;
		if (ds < 0 || (ds <= 8 && basetime >= ds * AS_PER_SEC + thr)) break;
		timeslice();
	}
	return st_frames - f0;
}

void st_frame(void) {
	frame_done = false;
	while (!frame_done) timeslice();
}
