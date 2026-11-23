// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// st48sp.c -- the speech board's 8035 (segaspeech.cpp): a 2K ROM at 0 and
// 0x800, MOVX reads the speech data (its bank on port 2) and writes the
// SP0250; port 1 the host's latch and T0's clear, T0 the latch's strobe,
// T1 the SP0250's DRQ. mcs48.inc with those (st.c's).
#define M48_NAME(x) sp48_##x
#define M48_ROM     st_speech_rom
#define M48_PMASK   0x7ff
#define M48_P1_R()  st_sp_p1_r()
#define M48_P2_R()  0xff
#define M48_BUS_R() 0xff
#define M48_T0()    st_sp_t0()
#define M48_T1()    st_sp_t1()
#define M48_EXT_R(a)    st_sp_rom_r(a)
#define M48_P1_W(d)     st_sp_p1_w(d)
#define M48_P2_W(d)     st_sp_p2_w(d)
#define M48_EXT_W(a, d) st_sp_write(a, d)
#include "mcs48.inc"
