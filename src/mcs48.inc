// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// mcs48.inc -- MAME 0.288's mcs48_cpu_device (mcs48.cpp) in C, for an
// 8035 (the 8048's opcodes, 64 bytes of RAM, the program on the external
// bus: EA=1, so A11 is SEL MB's), the timer/counter and both interrupts.
// From Donkey Kong's (Parrot Cart's dkong/mcs48.c), made a template so a
// board can have two: the file that includes it defines
//   M48_NAME(x)        the public names (st48usb.c: usb48_##x)
//   M48_ROM            the program (a pointer expression), M48_PMASK its
//                      address mask (0xfff: 4K; 0x7ff: 2K, mirrored)
//   M48_P1_R() M48_P2_R() M48_BUS_R() M48_T0() M48_T1() M48_EXT_R(a)
//                      the inputs: ports 1 and 2, the bus, T0, T1, MOVX A,@R
//   M48_P1_W(d) M48_P2_W(d) M48_EXT_W(a, d)
//                      the outputs: ports 1 and 2, MOVX @R,A (each called
//                      with the cycles so far in the slice up to date, so
//                      it can take the time, and may end the slice)
// Each instruction takes its cycles and then does what it does, as MAME's
// does, so the board sees a port write at the same time MAME's does.
// MAME's license is in LICENSE (THIRD-PARTY.md).
#include "st48.h"


#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define MCS48_HOT __attribute__((section(".time_critical.mcs48")))
#else
#define MCS48_HOT
#endif

typedef uint8_t u8;
typedef uint16_t u16;

#define TIMER_ENABLED   0x01
#define COUNTER_ENABLED 0x02
#define C_FLAG 0x80
#define A_FLAG 0x40
#define F_FLAG 0x20
#define B_FLAG 0x10

enum { EXP_READ = 0, EXP_WRITE = 1, EXP_OR = 2, EXP_AND = 3 };

void (*M48_NAME(trace))(void);

static struct {
	u16 prevpc, pc;
	u8 a, psw, p1, p2, timer, prescaler, t1_history, a11, timecount_enabled;
	bool f1, irq_state, irq_polled, irq_in_progress, timer_overflow, timer_flag, tirq_enabled, xirq_enabled;
	u8 ram[64];
	int icount, cycles_running, cycles_stolen;
} M;

#define RAM(a) M.ram[(a) & 0x3f]

// (each helper on a pc and an icount given, which execute keeps in
// registers, and on M's for the rest)
static inline __attribute__((always_inline)) u8 fetch_l(const u8 *rom, u16 *pc) {
	u16 address = *pc;
	*pc = (u16)(((address + 1) & 0x7ff) | (address & 0x800));
	return rom[address & M48_PMASK];
}

static inline __attribute__((always_inline)) void burn_l(int *ic, int count) {
	if (M.timecount_enabled) {
		bool timerover = false;
		if (M.timecount_enabled & TIMER_ENABLED) {
			u8 oldtimer = M.timer;
			M.prescaler = (u8)(M.prescaler + count);
			M.timer = (u8)(M.timer + (M.prescaler >> 5));
			M.prescaler &= 0x1f;
			timerover = M.timer < oldtimer;
		} else if (M.timecount_enabled & COUNTER_ENABLED) {
			for (; count > 0; count--, (*ic)--) {
				M.t1_history = (u8)((M.t1_history << 1) | (M48_T1() & 1));
				if ((M.t1_history & 3) == 2)
					if (++M.timer == 0) timerover = true;
			}
		}
		if (timerover) {
			M.timer_flag = true;
			if (M.tirq_enabled) M.timer_overflow = true;
		}
	}
	*ic -= count;
}
static inline void burn_cycles(int count) { burn_l(&M.icount, count); }

static inline __attribute__((always_inline)) void push_pc_psw_l(u16 pc) {
	u8 sp = M.psw & 0x07;
	RAM(8 + 2 * sp) = (u8)pc;
	RAM(9 + 2 * sp) = (u8)(((pc >> 8) & 0x0f) | (M.psw & 0xf0));
	M.psw = (u8)((M.psw & 0xf0) | ((sp + 1) & 0x07));
}

static inline __attribute__((always_inline)) void pull_pc_psw_l(u16 *pc) {
	u8 sp = (M.psw - 1) & 0x07;
	u16 v = RAM(8 + 2 * sp);
	v |= (u16)(RAM(9 + 2 * sp) << 8);
	M.psw = (u8)(((v >> 8) & 0xf0) | sp);
	*pc = v & (M.irq_in_progress ? 0x7ff : 0xfff);
}

static inline __attribute__((always_inline)) void pull_pc_l(u16 *pc) {
	u8 sp = (M.psw - 1) & 0x07;
	u16 v = RAM(8 + 2 * sp);
	v |= (u16)(RAM(9 + 2 * sp) << 8);
	*pc = v & (M.irq_in_progress ? 0x7ff : 0xfff);
	M.psw = (u8)((M.psw & 0xf0) | sp);
}

static inline void op_add(u8 dat) {
	u16 temp = (u16)(M.a + dat);
	u16 temp4 = (u16)((M.a & 0x0f) + (dat & 0x0f));
	M.psw &= (u8)~(C_FLAG | A_FLAG);
	M.psw |= (u8)((temp4 << 2) & A_FLAG);
	M.psw |= (u8)((temp >> 1) & C_FLAG);
	M.a = (u8)temp;
}

static inline void op_addc(u8 dat) {
	u8 carryin = (M.psw & C_FLAG) >> 7;
	u16 temp = (u16)(M.a + dat + carryin);
	u16 temp4 = (u16)((M.a & 0x0f) + (dat & 0x0f) + carryin);
	M.psw &= (u8)~(C_FLAG | A_FLAG);
	M.psw |= (u8)((temp4 << 2) & A_FLAG);
	M.psw |= (u8)((temp >> 1) & C_FLAG);
	M.a = (u8)temp;
}

static inline __attribute__((always_inline)) void jmp_l(u16 *pc, u16 address) {
	u16 a11 = M.irq_in_progress ? 0 : M.a11 ? 0x800 : 0;
	*pc = address | a11;
}

static inline __attribute__((always_inline)) void call_l(u16 *pc, u16 address) {
	push_pc_psw_l(*pc);
	jmp_l(pc, address);
}

static inline __attribute__((always_inline)) void jcc_l(const u8 *rom, u16 *pc, bool result) {
	u16 pch = *pc & 0xf00;
	u8 offset = fetch_l(rom, pc);
	if (result) *pc = pch | offset;
}
static inline void op_call(u16 address) { call_l(&M.pc, address); }
static inline void op_jcc(bool result) { jcc_l(M48_ROM, &M.pc, result); }

static void expander(int operation, int port) {
	M.p2 = (u8)((M.p2 & 0xf0) | (operation << 2) | (port & 3));
	M48_P2_W(M.p2);
	// (PROG: not connected)
	if (operation != EXP_READ) {
		M.p2 = (u8)((M.p2 & 0xf0) | (M.a & 0x0f));
		M48_P2_W(M.p2);
	} else {
		M.p2 |= 0x0f;
		M48_P2_W(M.p2);
		M.a = M48_P2_R() & 0x0f;
	}
}

static void check_irqs(void) {
	if (M.irq_in_progress) return;
	if (M.irq_state && M.xirq_enabled) {
		burn_cycles(2);
		M.irq_in_progress = true;
		if (M.irq_polled) {     // force JNI to be taken (MAME's hack)
			M.pc = (u16)(((M.prevpc + 1) & 0x7ff) | (M.prevpc & 0x800));
			op_jcc(true);
		}
		op_call(0x03);
	} else if (M.timer_overflow && M.tirq_enabled) {
		burn_cycles(2);
		M.irq_in_progress = true;
		op_call(0x07);
		M.timer_overflow = false;
	}
}

void M48_NAME(start)(void) {
	memset(&M, 0, sizeof M);
}

void M48_NAME(reset)(void) {
	M.pc = 0;
	M.psw = M.psw & (C_FLAG | A_FLAG);
	M.f1 = false;
	M.a11 = 0;
	M.tirq_enabled = false;
	M.xirq_enabled = false;
	M.timecount_enabled = 0;
	M.timer_flag = false;
	M.irq_in_progress = false;
	M.timer_overflow = false;
	M.irq_polled = false;
	// ports 1 and 2 to input (the bus's write goes nowhere)
	M.p1 = 0xff;
	M.p2 = 0xff;
	M48_P1_W(M.p1);
	M48_P2_W(M.p2);
}

void M48_NAME(set_irq)(int state) { M.irq_state = state != 0; }

#define RB (M.ram + ((M.psw & B_FLAG) ? 24 : 0))

// each opcode's cycles (illegal ones 1): MAME's handlers all burn them first
static const u8 op_cycles[256] = {
	1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static MCS48_HOT __attribute__((noinline)) void execute(void) {
	// pc, icount, the last pc and the JNI flag in registers: M's at the
	// calls that see them (check_irqs, the port writes, which take the time
	// and may end the slice, and the trace), and at the end
	// The timer too: while it runs (STRT T), the icount at which it
	// overflows (tdead: icount less the cycles left to the overflow, its 8
	// bits and the prescaler's 5 as one count), so a burn is a subtract and
	// a compare; M's timer and prescaler made from it where they're read
	// (MOV A,T, a change of its mode or value, check_irqs, the end). As a
	// counter (STRT CNT) every burn takes the slow way; stopped, none. The
	// IRQ line only changes between slices (a sync).
	const u8 *rom = M48_ROM;
	u16 pc = M.pc, prevpc = M.prevpc;
	int ic = M.icount;
	bool polled = M.irq_polled;
	const bool irq = M.irq_state;
	bool pend = irq | M.timer_overflow;     // check_irqs has something to look at
	int tmode, tdead;
#define TD_NEVER  (-0x40000000)
#define TD_ALWAYS 0x7fffffff
#define T_IN() do {                                                                 \
		tmode = M.timecount_enabled;                                                \
		tdead = tmode == TIMER_ENABLED ? ic - (8192 - ((M.timer << 5) | (M.prescaler & 0x1f))) \
		      : tmode ? TD_ALWAYS : TD_NEVER;                                       \
	} while (0)
#define T_OUT() do { if (tmode == TIMER_ENABLED) { int tpos_ = 8192 - (ic - tdead); M.timer = (u8)(tpos_ >> 5); M.prescaler = (u8)(tpos_ & 0x1f); } } while (0)
	T_IN();
#define fetch() fetch_l(rom, &pc)
#define burn_cycles(n) do {                                                         \
		ic -= (n);                                                                  \
		if (__builtin_expect(ic <= tdead, 0)) {                                     \
			if (tmode == TIMER_ENABLED) {                                           \
				tdead -= 8192;                                                      \
				M.timer_flag = true;                                                \
				if (M.tirq_enabled) M.timer_overflow = pend = true;                 \
			} else { ic += (n); burn_l(&ic, n); pend = irq | M.timer_overflow; }    \
		}                                                                           \
	} while (0)
#define op_jmp(a) jmp_l(&pc, a)
#define op_call(a) call_l(&pc, a)
#define op_jcc(c) jcc_l(rom, &pc, c)
#define pull_pc() pull_pc_l(&pc)
#define pull_pc_psw() pull_pc_psw_l(&pc)
#define prog(a) rom[(a) & M48_PMASK]
// (a port write can end the slice: icount to 0, the timer where it was)
#define PORT(x) do { M.icount = ic; x; if (tmode == TIMER_ENABLED) tdead += M.icount - ic; ic = M.icount; } while (0)
	do {
		if (__builtin_expect(pend, 0)) {     // (nothing to take otherwise)
			M.pc = pc; M.prevpc = prevpc; M.icount = ic; M.irq_polled = polled; T_OUT();
			check_irqs();
			pc = M.pc; ic = M.icount; T_IN();
			pend = irq | M.timer_overflow;
		}
		polled = false;
		prevpc = pc;
#ifndef M48_NO_TRACE
		if (M48_NAME(trace)) { M.pc = pc; M.prevpc = prevpc; M.icount = ic; M.irq_polled = false; T_OUT(); M48_NAME(trace)(); }
#endif
		u8 op = fetch();
#define r RB                            // (the register bank, where an instruction uses it)
		u8 t;
		burn_cycles(op_cycles[op]);
		switch (op) {
		case 0x00: break;                                       // nop
		case 0x02: break;                                       // outl bus,a (nothing on the bus)
		case 0x03: op_add(fetch()); break;
		case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4:
			t = fetch(); op_jmp((u16)(t | ((op & 0xe0) << 3))); break;
		case 0x05: M.xirq_enabled = true; break;
		case 0x07: M.a--; break;
		case 0x08: M.a = M48_BUS_R(); break;                   // ins a,bus
		case 0x09: M.a = M48_P1_R() & M.p1; break;              // in a,p1
		case 0x0a: M.a = M48_P2_R() & M.p2; break;           // in a,p2
		case 0x0c: case 0x0d: case 0x0e: case 0x0f: PORT(expander(EXP_READ, op & 3)); break;
		case 0x10: case 0x11: RAM(r[op & 1]) = (u8)(RAM(r[op & 1]) + 1); break;
		case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
			op_jcc((M.a & (1u << (op >> 5))) != 0); break;
		case 0x13: op_addc(fetch()); break;
		case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4:
			t = fetch(); op_call((u16)(t | ((op & 0xe0) << 3))); break;
		case 0x15: M.xirq_enabled = false; break;
		case 0x16: op_jcc(M.timer_flag); M.timer_flag = false; break;
		case 0x17: M.a++; break;
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			r[op & 7]++; break;
		case 0x20: case 0x21: t = M.a; M.a = RAM(r[op & 1]); RAM(r[op & 1]) = t; break;
		case 0x23: M.a = fetch(); break;
		case 0x25: M.tirq_enabled = true; break;
		case 0x26: op_jcc(M48_T0() == 0); break;
		case 0x27: M.a = 0; break;
		case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
			t = M.a; M.a = r[op & 7]; r[op & 7] = t; break;
		case 0x30: case 0x31: {
			u8 oldram = RAM(r[op & 1]);
			RAM(r[op & 1]) = (u8)((oldram & 0xf0) | (M.a & 0x0f));
			M.a = (u8)((M.a & 0xf0) | (oldram & 0x0f));
			break;
		}
		case 0x35: M.tirq_enabled = false; M.timer_overflow = false; pend = irq; break;
		case 0x36: op_jcc(M48_T0() != 0); break;
		case 0x37: M.a ^= 0xff; break;
		case 0x39: M.p1 = M.a; PORT(M48_P1_W(M.p1)); break;
		case 0x3a: M.p2 = M.a; PORT(M48_P2_W(M.p2)); break;
		case 0x3c: case 0x3d: case 0x3e: case 0x3f: PORT(expander(EXP_WRITE, op & 3)); break;
		case 0x40: case 0x41: M.a |= RAM(r[op & 1]); break;
		case 0x42: T_OUT(); M.a = M.timer; break;
		case 0x43: M.a |= fetch(); break;
		case 0x45:
			T_OUT();
			if (!(M.timecount_enabled & COUNTER_ENABLED)) M.t1_history = (u8)M48_T1();
			M.timecount_enabled = COUNTER_ENABLED;
			T_IN();
			break;
		case 0x46: op_jcc(M48_T1() == 0); break;
		case 0x47: M.a = (u8)((M.a << 4) | (M.a >> 4)); break;
		case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
			M.a |= r[op & 7]; break;
		case 0x50: case 0x51: M.a &= RAM(r[op & 1]); break;
		case 0x53: M.a &= fetch(); break;
		case 0x55: T_OUT(); M.timecount_enabled = TIMER_ENABLED; M.prescaler = 0; T_IN(); break;
		case 0x56: op_jcc(M48_T1() != 0); break;
		case 0x57:
			if ((M.a & 0x0f) > 0x09 || (M.psw & A_FLAG)) {
				if (M.a > 0xf9) M.psw |= C_FLAG;
				M.a += 0x06;
			}
			if ((M.a & 0xf0) > 0x90 || (M.psw & C_FLAG)) {
				M.a += 0x60;
				M.psw |= C_FLAG;
			}
			break;
		case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
			M.a &= r[op & 7]; break;
		case 0x60: case 0x61: op_add(RAM(r[op & 1])); break;
		case 0x62: T_OUT(); M.timer = M.a; T_IN(); break;
		case 0x65: T_OUT(); M.timecount_enabled = 0; T_IN(); break;
		case 0x67: {
			u8 newc = (u8)((M.a << 7) & C_FLAG);
			M.a = (u8)((M.a >> 1) | (M.psw & C_FLAG));
			M.psw = (u8)((M.psw & ~C_FLAG) | newc);
			break;
		}
		case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
			op_add(r[op & 7]); break;
		case 0x70: case 0x71: op_addc(RAM(r[op & 1])); break;
		case 0x75: break;                                       // ent0 clk (no T0 clock function)
		case 0x76: op_jcc(M.f1); break;
		case 0x77: M.a = (u8)((M.a >> 1) | (M.a << 7)); break;
		case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
			op_addc(r[op & 7]); break;
		case 0x80: case 0x81: M.a = M48_EXT_R(r[op & 1]); break;
		case 0x83: pull_pc(); break;
		case 0x85: M.psw &= (u8)~F_FLAG; break;
		case 0x86: polled = !irq; op_jcc(irq); break;
		case 0x88: M48_BUS_R(); fetch(); break;              // orl bus,#n (nothing on the bus)
		case 0x89: M.p1 |= fetch(); PORT(M48_P1_W(M.p1)); break;
		case 0x8a: M.p2 |= fetch(); PORT(M48_P2_W(M.p2)); break;
		case 0x8c: case 0x8d: case 0x8e: case 0x8f: PORT(expander(EXP_OR, op & 3)); break;
		case 0x90: case 0x91: PORT(M48_EXT_W(r[op & 1], M.a)); break;     // movx @r,a
		case 0x93: M.irq_in_progress = false; pull_pc_psw(); break;
		case 0x95: M.psw ^= F_FLAG; break;
		case 0x96: op_jcc(M.a != 0); break;
		case 0x97: M.psw &= (u8)~C_FLAG; break;
		case 0x98: M48_BUS_R(); fetch(); break;              // anl bus,#n
		case 0x99: M.p1 &= fetch(); PORT(M48_P1_W(M.p1)); break;
		case 0x9a: M.p2 &= fetch(); PORT(M48_P2_W(M.p2)); break;
		case 0x9c: case 0x9d: case 0x9e: case 0x9f: PORT(expander(EXP_AND, op & 3)); break;
		case 0xa0: case 0xa1: RAM(r[op & 1]) = M.a; break;
		case 0xa3: M.a = prog((u16)((pc & 0xf00) | M.a)); break;
		case 0xa5: M.f1 = false; break;
		case 0xa7: M.psw ^= C_FLAG; break;
		case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
			r[op & 7] = M.a; break;
		case 0xb0: case 0xb1: t = fetch(); RAM(r[op & 1]) = t; break;
		case 0xb3: pc &= 0xf00; pc |= prog((u16)(pc | M.a)); break;
		case 0xb5: M.f1 = !M.f1; break;
		case 0xb6: op_jcc((M.psw & F_FLAG) != 0); break;
		case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
			r[op & 7] = fetch(); break;
		case 0xc5: M.psw &= (u8)~B_FLAG; break;
		case 0xc6: op_jcc(M.a == 0); break;
		case 0xc7: M.a = M.psw | 0x08; break;
		case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
			r[op & 7]--; break;
		case 0xd0: case 0xd1: M.a ^= RAM(r[op & 1]); break;
		case 0xd3: M.a ^= fetch(); break;
		case 0xd5: M.psw |= B_FLAG; break;
		case 0xd7: M.psw = M.a & (u8)~0x08; break;
		case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
			M.a ^= r[op & 7]; break;
		case 0xe3: M.a = prog((u16)(0x300 | M.a)); break;
		case 0xe5: M.a11 = 0; break;
		case 0xe6: op_jcc((M.psw & C_FLAG) == 0); break;
		case 0xe7: M.a = (u8)((M.a << 1) | (M.a >> 7)); break;
		case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
			op_jcc(--r[op & 7] != 0); break;
		case 0xf0: case 0xf1: M.a = RAM(r[op & 1]); break;
		case 0xf5: M.a11 = 1; break;
		case 0xf6: op_jcc((M.psw & C_FLAG) != 0); break;
		case 0xf7: {
			u8 newc = M.a & C_FLAG;
			M.a = (u8)((M.a << 1) | (M.psw >> 7));
			M.psw = (u8)((M.psw & ~C_FLAG) | newc);
			break;
		}
		case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
			M.a = r[op & 7]; break;
		default: break;                                         // illegal
		}
	} while (ic > 0);
#undef fetch
#undef burn_cycles
#undef op_jmp
#undef op_call
#undef op_jcc
#undef pull_pc
#undef pull_pc_psw
#undef prog
#undef PORT
#undef r
#undef T_IN
	T_OUT();
#undef T_OUT
	M.pc = pc;
	M.prevpc = prevpc;
	M.icount = ic;
	M.irq_polled = polled;
}

int M48_NAME(run)(int cycles) {
	M.cycles_running = cycles;
	M.cycles_stolen = 0;
	M.icount = cycles;
	execute();
	return cycles - M.icount - M.cycles_stolen;
}

void M48_NAME(abort)(void) {
	if (M.icount > 0) {
		M.cycles_stolen += M.icount;
		M.cycles_running -= M.icount;
		M.icount = 0;
	}
}

int M48_NAME(done)(void) { return M.cycles_running - M.icount; }

void M48_NAME(regs)(st48_regs_t *r) {
	r->pc = M.pc;
	r->a = M.a;
	r->psw = M.psw;
	r->p1 = M.p1;
	r->p2 = M.p2;
	r->timer = M.timer;
	r->prescaler = M.prescaler;
	r->f1 = M.f1;
	r->a11 = M.a11;
	memcpy(r->ram, M.ram, 64);
}
