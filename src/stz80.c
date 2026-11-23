// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// stz80.c -- the board's main Z80 (3.86712 MHz): z80.c built with the Sega
// G80 bus (stz80.h). The hooks are st.c's.
#include "stz80.h"

const uint8_t *stz_rd_page[256];
uint8_t *stz_wr_page[256];
void (*stz_trace)(void);

#define Z80_CPUS 1
#define Z80_SEGA_G80
#define z80_rd_pages(c)    ((void)(c), stz_rd_page)
#define z80_wr_pages(c)    ((void)(c), stz_wr_page)
static const z80_hooks_t hooks = {stz_hook_rd, stz_hook_wr, stz_hook_in, stz_hook_out, stz_hook_irq_ack};
#define z80_hooks(c)       ((void)(c), &hooks)
#define z80_trace(c)       ((void)(c), stz_trace)
#include "z80.c"
