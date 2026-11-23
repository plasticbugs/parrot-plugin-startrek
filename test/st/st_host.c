// st_host.c -- the Star Trek board model on the Mac, for the tests against MAME.
//
//   st_host ROMDIR [-frames N] [-script FILE] [-dump FILE] [-trace FILE]
//           [-utrace FILE] [-strace FILE] [-sndlog FILE] [-bot FROM -record FILE]
//           [-wav FILE]
//
// ROMDIR holds the set's files, unzipped (test/st/setup.sh makes one under
// $TMPDIR). -dump writes the records stdump.lua writes in MAME; -trace,
// -utrace and -strace the main CPU's, the USB's and the speech board's
// traces; -sndlog each write to the sound chips (the SP0250's samples and
// the USB's registers, with their times). Script lines: "frame name value"
// (stdump.lua's names: coin1 coin2 service start1 start2 b1 b2 b3 b4, 1
// pressed; dial, the spinner's move a frame, -128..127), from that frame on.
// -bot plays from frame FROM on (two coins, start, then turns, fires and
// moves in a fixed pattern), and -record writes what it pressed as a
// script, for MAME to play the same.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st.h"
#include "st48.h"
#include "stz80.h"

static uint8_t maincpu[0xc000], speech_cpu[0x800], speech_data[0x4000], sine[0x400];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "st_host: can't read %s\n", path); exit(1); }
	fclose(f);
}

static FILE *trace, *utrace, *strace, *sndlog;
static int cur_frame;

static void minsn(void) {
	z80_regs_t r;
	stz_regs(&r);
	fprintf(trace, "@ %04X %04X %04X %04X %04X %04X %04X %04X %llu\n", r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy,
	        (unsigned long long)st_cycles());
}
static void uinsn(void) {
	st48_regs_t r;
	st_usb_regs(&r);
	fprintf(utrace, "@ %03X %02X %llu\n", r.pc, r.a, (unsigned long long)st_usb_cycles());
}
static void sinsn(void) {
	st48_regs_t r;
	st_speech_regs(&r);
	fprintf(strace, "@ %03X %02X %llu\n", r.pc, r.a, (unsigned long long)st_speech_cycles());
}

// the sound chips' inputs, for the sound's tests (stsnd.c's on the cart)
static int8_t sp_samples[1 << 16];
static uint32_t sp_n;
void st_sp_sample(uint32_t index, int8_t dac) {
	if (sndlog) fprintf(sndlog, "S %u %d\n", index, dac);
	sp_samples[index & 0xffff] = dac;
	sp_n = index + 1;
}
void st_usb_write(uint64_t tick, int reg, uint8_t value) {
	if (sndlog) fprintf(sndlog, "U %llu %02x %02x\n", (unsigned long long)tick, reg, value);
}
void st_gain_write(uint64_t tick, uint8_t control) {
	if (sndlog) fprintf(sndlog, "G %llu %02x\n", (unsigned long long)tick, control);
}

static const struct { const char *name; int port; uint8_t mask; } FIELDS[] = {
	{"coin1", 0, 0x01}, {"coin2", 0, 0x10}, {"service", 1, 0x01},
	{"start1", 2, 0x01}, {"start2", 2, 0x02}, {"b2", 2, 0x04}, {"b1", 2, 0x08}, {"b3", 2, 0x10}, {"b4", 2, 0x20}};

static void apply(const char *name, int v) {
	if (!strcmp(name, "dial")) { st_in.dial = (int8_t)v; return; }
	for (size_t i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
		if (strcmp(name, FIELDS[i].name)) continue;
		uint8_t *p = FIELDS[i].port == 0 ? &st_in.d7d6 : FIELDS[i].port == 1 ? &st_in.d5d4 : &st_in.fc;
		bool active_low = FIELDS[i].port < 2;
		bool bit = active_low ? !v : v != 0;
		*p = (uint8_t)(bit ? *p | FIELDS[i].mask : *p & ~FIELDS[i].mask);
		return;
	}
	fprintf(stderr, "st_host: no input %s\n", name);
	exit(2);
}

// the bot: what each input is at frame n (-1: as it was)
static int bot_value(const char *name, int n) {
	static const int coin_at[] = {0, 40, 80};
	if (!strcmp(name, "coin1")) {
		for (int i = 1; i < 3; i++) if (n == coin_at[i]) return 1; else if (n == coin_at[i] + 4) return 0;
		return -1;
	}
	if (!strcmp(name, "start1")) return n == 140 ? 1 : n == 144 ? 0 : -1;
	if (n < 200) return -1;
	int t = n - 200;
	if (!strcmp(name, "dial")) {            // turns one way, then the other, at different speeds
		static const int8_t turn[8] = {3, 0, -5, 2, 0, 8, -2, -9};
		return turn[(t / 37) % 8];
	}
	if (!strcmp(name, "b1")) return (t % 9) < 3;            // phasers
	if (!strcmp(name, "b2")) return (t % 150) < 4;          // a photon torpedo
	if (!strcmp(name, "b3")) return (t % 400) < 90;         // impulse
	if (!strcmp(name, "b4")) return (t % 1300) > 1250;      // warp
	return -1;
}

static void regs48(uint8_t **pp, int which) {
	st48_regs_t r;
	if (which) st_speech_regs(&r); else st_usb_regs(&r);
	uint8_t *p = *pp;
	*p++ = (uint8_t)r.pc; *p++ = (uint8_t)(r.pc >> 8);
	*p++ = r.a; *p++ = 0; *p++ = r.timer;     // (MAME's Lua has no PSW)
	 *p++ = r.prescaler; *p++ = r.p1; *p++ = r.p2;
	memcpy(p, r.ram, 64); p += 64;
	*pp = p;
}

// at each VBLANK, where MAME's Lua frame_done runs (before the ports
// take the new values): the script's and the bot's inputs for that frame
static struct { int frame; char name[16]; int v; } ev[65536];
static int nev, botfrom;
static FILE *record;
static FILE *segs;
static void on_vblank(void) {
	int n = (int)st_frames;
	if (segs) {                         // the frame's vectors: u32 frame, u16 count, then each's x0 y0 x1 y1 ox oy (s16) colour symbol
		uint32_t f = (uint32_t)n;
		fwrite(&f, 4, 1, segs);
		fwrite(&st_nsegs, 2, 1, segs);
		for (int i = 0; i < st_nsegs; i++) {
			const st_seg_t *g = &st_segs[i];
			int16_t c[6] = {g->x0, g->y0, g->x1, g->y1, g->ox, g->oy};
			fwrite(c, 2, 6, segs);
			fwrite(&g->colour, 1, 1, segs);
			fwrite(&g->symbol, 1, 1, segs);
		}
	}
	for (int i = 0; i < nev; i++) {
		if (ev[i].frame != n) continue;
		apply(ev[i].name, ev[i].v);
		if (record) fprintf(record, "%d %s %d\n", n, ev[i].name, ev[i].v);
	}
	if (botfrom && n >= botfrom) {
		static const char *names[] = {"coin1", "start1", "dial", "b1", "b2", "b3", "b4"};
		static int last[7] = {0, 0, 0, 0, 0, 0, 0};
		for (int k = 0; k < 7; k++) {
			int v = bot_value(names[k], n);
			if (v < 0 || v == last[k]) continue;
			last[k] = v;
			apply(names[k], v);
			if (record) fprintf(record, "%d %s %d\n", n, names[k], v);
		}
	}
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: st_host ROMDIR [options]\n"); return 2; }
	const char *dir = argv[1], *script = NULL, *dumpname = NULL;
	int frames = 600;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-frames")) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-script")) script = argv[++i];
		else if (!strcmp(argv[i], "-dump")) dumpname = argv[++i];
		else if (!strcmp(argv[i], "-trace")) trace = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-utrace")) utrace = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-strace")) strace = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-sndlog")) sndlog = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-record")) record = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-bot")) botfrom = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-segs")) segs = fopen(argv[++i], "wb");
		else { fprintf(stderr, "st_host: what is %s?\n", argv[i]); return 2; }
	}
	load(dir, "1873.cpu-u25", maincpu, 0x800);
	for (int i = 0; i < 23; i++) {
		char name[32];
		snprintf(name, sizeof name, "%d.prom-u%d", 1848 + i, 1 + i);
		load(dir, name, maincpu + 0x800 + 0x800 * i, 0x800);
	}
	load(dir, "1607.speech-u7", speech_cpu, 0x800);
	load(dir, "1871.speech-u6", speech_data, 0x1000);
	load(dir, "1872.speech-u5", speech_data + 0x1000, 0x1000);
	load(dir, "s-c.xyt-u39", sine, 0x400);
	st_roms_t roms = {maincpu, speech_cpu, speech_data, sine};
	if (script) {
		FILE *f = fopen(script, "r");
		if (!f) { fprintf(stderr, "st_host: can't read %s\n", script); return 1; }
		char line[256];
		while (fgets(line, sizeof line, f) && nev < 65536)
			if (sscanf(line, "%d %15s %d", &ev[nev].frame, ev[nev].name, &ev[nev].v) == 3) nev++;
		fclose(f);
	}
	if (trace) stz_trace = minsn;
	if (utrace) st_usb_trace = uinsn;
	if (strace) st_speech_trace = sinsn;
	st_on_vblank = on_vblank;
	st_init(&roms);
	FILE *dump = dumpname ? fopen(dumpname, "wb") : NULL;
	for (int n = 1; n <= frames; n++) {
		cur_frame = n;
		st_frame();                     // (on_vblank gave it frame n's inputs, where MAME's Lua does)
		if (dump) {
			static uint8_t rec[8 + 0x800 + 0x1000 + 0x1000 + 0x400 + 32 + 2 * 72];
			uint8_t *p = rec;
			memcpy(p, "STFR", 4); p += 4;
			uint32_t fn = (uint32_t)n; memcpy(p, &fn, 4); p += 4;
			memcpy(p, st_ram, 0x800); p += 0x800;
			memcpy(p, st_vram, 0x1000); p += 0x1000;
			memcpy(p, st_usb_pgm, 0x1000); p += 0x1000;
			memcpy(p, st_usb_work, 0x400); p += 0x400;
			z80_regs_t r;
			stz_regs(&r);
			uint16_t w[13] = {r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy, r.af2, r.bc2, r.de2, r.hl2, r.wz};
			memcpy(p, w, 26); p += 26;
			*p++ = r.r; *p++ = r.i; *p++ = r.im; *p++ = r.iff1; *p++ = r.iff2; *p++ = r.halt;
			regs48(&p, 0);
			regs48(&p, 1);
			fwrite(rec, 1, (size_t)(p - rec), dump);
		}
	}
	if (dump) fclose(dump);
	if (trace) fclose(trace);
	if (utrace) fclose(utrace);
	if (strace) fclose(strace);
	if (sndlog) fclose(sndlog);
	if (record) fclose(record);
	fprintf(stderr, "st_host: %d frames, %llu main cycles, %llu USB cycles, %llu speech cycles, %u SP0250 samples\n", frames,
	        (unsigned long long)st_cycles(), (unsigned long long)st_usb_cycles(), (unsigned long long)st_speech_cycles(), sp_n);
	return 0;
}
