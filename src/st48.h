// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// st48.h -- the board's two 8035s (mcs48.inc): usb48_* the Universal
// Sound Board's (6 MHz: 4K of program RAM the main CPU loads), sp48_* the
// speech board's (3.12 MHz: a 2K ROM, mirrored). Their ports are st.c's.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "st.h"

#define ST48_DECLARE(p) \
	void p##_start(void);               /* device_start */ \
	void p##_reset(void);               /* device_reset */ \
	int p##_run(int cycles);            /* execute_run for a timeslice; the cycles run */ \
	void p##_abort(void);               /* abort_timeslice */ \
	int p##_done(void);                 /* cycles run so far in this timeslice */ \
	void p##_set_irq(int state);        /* MCS48_INPUT_IRQ */ \
	void p##_regs(st48_regs_t *r); \
	extern void (*p##_trace)(void);
ST48_DECLARE(usb48)
ST48_DECLARE(sp48)

// st.c's side
extern uint8_t st_usb_pgm[0x1000];
uint8_t st_usb_p1_r(void);
void st_usb_p1_w(uint8_t d);
void st_usb_p2_w(uint8_t d);
int st_usb_t1(void);
uint8_t st_usb_work_r(uint8_t a);
void st_usb_work_w(uint8_t a, uint8_t d);
extern const uint8_t *st_speech_rom;
uint8_t st_sp_p1_r(void);
void st_sp_p1_w(uint8_t d);
void st_sp_p2_w(uint8_t d);
int st_sp_t0(void);
int st_sp_t1(void);
uint8_t st_sp_rom_r(uint8_t a);
void st_sp_write(uint8_t a, uint8_t d);
