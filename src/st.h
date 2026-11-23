// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Nicola Salmoria
//
// st.h -- the Star Trek board (Sega G80 vector, 1982) as MAME 0.288
// emulates it (segag80v.cpp, segag80v_v.cpp, segag80_m.cpp, segausb.cpp,
// segaspeech.cpp, sp0250.cpp), with MAME's scheduler, so the three CPUs
// run the cycles MAME's do: the main Z80, the Universal Sound Board's 8035
// and the speech board's 8035. The sound they make is stsnd.c's: this file
// hands it each write to the sound chips with its time.
#ifndef ST_BOARD_H
#define ST_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define ST_CLOCK 3867120u               // the main Z80: VIDEO_CLOCK / 4 (15.46848 MHz)
#define ST_USB_CPS 400000u              // the USB's 8035: 6 MHz / 15
#define ST_SPEECH_CPS 208000u           // the speech board's 8035: 3.12 MHz / 15
#define ST_SP_RATE 10000u               // the SP0250's samples a second
#define ST_USB_RATE 2000000u            // the USB's stream (its 2 MHz clock)

typedef struct {
	const uint8_t *maincpu;             // 0xc000: 1873.cpu-u25, then 1848-1870 (the PROM board)
	const uint8_t *speech_cpu;          // 0x800: 1607.speech-u7
	const uint8_t *speech_data;         // 0x4000: 1871.speech-u6, 1872.speech-u5, then zeros
	const uint8_t *sine;                // 0x400: s-c.xyt-u39 (the vector generator's sine PROM)
} st_roms_t;

// The ports as MAME has them (segag80v.cpp's g80v_generic and startrek):
// D7D6 0x01 coin 1, 0x10 coin 2 (active low; the draw line, 0x20, is the
// board's own), D5D4 0x01 service (active low), D3D2 and D1D0 the DIP
// switches, FC active high: 0x01 start 1, 0x02 start 2, 0x08 button 1
// (phasers), 0x04 button 2 (photon torpedoes), 0x10 button 3 (impulse),
// 0x20 button 4 (warp). dial: the spinner's move this frame (MAME's
// IPT_DIAL with PORT_RESET: each read of it in a frame sees the same).
// Each takes effect at the next VBLANK, as MAME's ports do.
typedef struct {
	uint8_t d7d6, d5d4, d3d2, d1d0, fc;
	int8_t dial;
} st_inputs_t;
// MAME's defaults: upright, demo sounds on, 4 photon torpedoes, easy,
// bonus at 30000; 1 coin 1 credit both ways
#define ST_D3D2 0x8d
#define ST_D1D0 0x33
extern st_inputs_t st_in;

void st_init(const st_roms_t *roms);    // power on
void st_frame(void);                    // run to the next VBLANK start

// The cart's way: run to the main CPU's cycle `until` (from power on),
// stopping at the first of MAME's timeslice ends at or past it. Returns
// the VBLANKs passed; st_on_vblank, if set, is called at each (where
// MAME's Lua frame_done runs: the vector list just made, the inputs not
// yet taken).
uint32_t st_run_to(uint64_t until);
uint64_t st_time(void);                 // where the board is, in main CPU cycles
extern void (*st_on_vblank)(void);

// RAM, as the CPUs see it
extern uint8_t st_ram[0x800];           // 0xc800
extern uint8_t st_vram[0x1000];         // 0xe000: the vector RAM
extern uint8_t st_usb_pgm[0x1000];      // 0xd000: the USB's program RAM
extern uint8_t st_usb_work[0x400];      // the USB's work RAM, 4 banks
extern uint32_t st_frames;              // VBLANKs so far

// The vector generator's sums for the frame drawn at the last VBLANK
// (MAME draws the list from the vector RAM as it is then)
typedef struct {
	uint16_t symbols, vectors;
	uint32_t steps;
	bool out_of_time;                   // the 39 Hz budget ran out before the list's end
} st_vstats_t;
extern st_vstats_t st_vstats;
extern const uint8_t *st_sine;          // the sine PROM

// What MAME's vector device draws for that frame: each vector where the
// beam is on, cut to where it is on the screen (adjust_xy's clipping), in
// MAME's screen units: x 0-1023 across, y 0-832 up from the bottom (the
// game's ORIENTATION_FLIP_Y: y 0 is the bottom of the picture; outside 0-832
// is off it). A dot is a line of no length. colour: RGB 2 bits each (bits
// 5-4 red, 3-2 green, 1-0 blue); symbol: the symbol it's in (its count in
// the list, from 1, mod 256).
typedef struct {
	int16_t x0, y0, x1, y1;
	uint8_t colour, symbol;
} st_seg_t;
#define ST_SEGS 1200
extern st_seg_t st_segs[ST_SEGS];
extern uint16_t st_nsegs;

// sound: the board calls these as MAME's devices get them. Times are in
// the chips' own ticks: the SP0250's samples (10 kHz) and the USB's 2 MHz
// stream; a write at tick n takes effect from sample n on.
void st_sp_sample(uint32_t index, int8_t dac);             // the SP0250's sample (index from power on)
void st_usb_write(uint64_t tick, int reg, uint8_t value);  // the USB's 8253s and envelopes (work RAM 0x00-0x17)
void st_gain_write(uint64_t tick, uint8_t control);        // the speech board's control port (bit 3 speech, bit 5 USB)
uint64_t st_usb_now(void);                                 // the USB stream's tick now

// for the tests
uint64_t st_cycles(void);               // the main CPU's cycles so far
uint64_t st_usb_cycles(void);
uint64_t st_speech_cycles(void);
uint8_t st_peek(uint16_t a);            // as the main CPU reads, without side effects
typedef struct {
	uint16_t pc;
	uint8_t a, psw, p1, p2, timer, prescaler, f1, a11;
	uint8_t ram[64];
} st48_regs_t;
void st_usb_regs(st48_regs_t *r);
void st_speech_regs(st48_regs_t *r);
extern void (*st_usb_trace)(void), (*st_speech_trace)(void);  // before each 8035 instruction
typedef struct {
	uint8_t in_latch, out_latch, last_p2, t1_clock;
	uint8_t sp_latch, sp_t0, sp_p2, sp_drq, sp_fifo_pos;
	uint8_t coin_ff, edgint_ff, spinner_count, spinner_select;
	bool usb_in_reset;
} st_misc_t;
void st_misc(st_misc_t *m);

#endif
