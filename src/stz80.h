// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// stz80.h -- Star Trek's main Z80 (z80.c, built by stz80.c with the
// board's bus: Z80_SEGA_G80). The board (st.c) supplies the page tables'
// pages and the hooks.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "z80.h"

extern const uint8_t *stz_rd_page[256];
extern uint8_t *stz_wr_page[256];
extern void (*stz_trace)(void);         // before each instruction (MAME's debugger_instruction_hook), tests only
extern uint16_t z80_scr_pc;             // the LD (nn),A being run, else 0xffff

uint8_t stz_hook_rd(uint16_t a);
void stz_hook_wr(uint16_t a, uint8_t d);
uint8_t stz_hook_in(uint16_t port);
void stz_hook_out(uint16_t port, uint8_t d);
uint8_t stz_hook_irq_ack(void);
static inline void stz_start(void) { z80_start(0); }
static inline void stz_reset(void) { z80_reset(0); }
static inline int stz_run(int cycles) { return z80_run(0, cycles); }
static inline void stz_abort(void) { z80_abort(0); }
static inline void stz_adjust_icount(int delta) { z80_adjust_icount(0, delta); }
static inline int stz_done(void) { return z80_done(0); }
static inline void stz_set_irq(int state) { z80_set_irq(0, state); }
static inline void stz_set_nmi(int state, bool ran) { z80_set_nmi(0, state, ran); }
static inline void stz_regs(z80_regs_t *r) { z80_regs(0, r); }
