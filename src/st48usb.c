// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// st48usb.c -- the Universal Sound Board's 8035 (segausb.cpp): its program
// in 4K of RAM, its work RAM through MOVX, port 1 the host's latch, port 2
// the work RAM's bank, the ready line and the T1 counter's clear; T1 the
// counter. mcs48.inc with those (st.c's).
#define M48_NAME(x) usb48_##x
#define M48_ROM     st_usb_pgm
#define M48_PMASK   0xfff
#define M48_P1_R()  st_usb_p1_r()
#define M48_P2_R()  0xff
#define M48_BUS_R() 0xff
#define M48_T0()    0
#define M48_T1()    st_usb_t1()
#define M48_EXT_R(a)    st_usb_work_r(a)
#define M48_P1_W(d)     st_usb_p1_w(d)
#define M48_P2_W(d)     st_usb_p2_w(d)
#define M48_EXT_W(a, d) st_usb_work_w(a, d)
#include "mcs48.inc"
