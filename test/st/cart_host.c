// cart_host.c -- the cart's side on the Mac: the board run a list at a time
// as the plugin runs it (src/strun.c), the picture and the sound through
// src/stcv.c. For previews, the lists' numbers and their sound. (Time
// Pilot's.)
//
//   cart_host ROMDIR [-lists N] [-script FILE] [-bot] [-png DIR -at L,L,...]
//             [-wav FILE] [-stats FILE] [-out LISTS.bin] [-from L]
//
// -script: st_host's script ("frame name value"), each line's input set at
// that frame's VBLANK as st_host and MAME set them. -bot: st_host's bot
// (two coins, 1P start, then turning and all four buttons on a pattern).
// -png: at each list in -at, what the console shows (cv_LIST.png, 256 x
// 192 doubled), the vectors of the picture it shows in their own colours
// at the same size (vec_LIST.png: MAME's list, a pixel wide, without the
// TMS9918's limits) and the two side by side (pair_LIST.png). -wav: the
// DAC's samples, the attenuators the lists carry as levels (the measured
// ladder) at the lists' 5,962 Hz. -out: the lists from list -from on, as
// test/host/host.c records them, for tools/mame-replay.sh. -stats: a line
// a list. All of these are made from the ROMs: keep them under $TMPDIR.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st.h"
#include "stcv.h"
#include "strun.h"

static uint8_t maincpu[0xc000], speech_cpu[0x800], speech_data[0x2000], sine[0x400];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "cart_host: can't read %s\n", path); exit(1); }
    fclose(f);
}

// ---- PNG, stored (no compression) ------------------------------------------------------
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be32(h, crc(crc(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}

static void png(const char *name, int w, int h, const uint8_t *rgb, int scale) {
    int W = w * scale, H = h * scale;
    size_t row = (size_t)W * 3 + 1, raw_n = row * H;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) memcpy(raw + y * row + 1 + x * 3, rgb + ((size_t)(y / scale) * w + x / scale) * 3, 3);
    }
    size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *p++ = off + n == raw_n;
        *p++ = (uint8_t)n; *p++ = (uint8_t)(n >> 8); *p++ = (uint8_t)~n; *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + off, n);
        p += n;
    }
    be32(p, b << 16 | a);
    p += 4;
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "cart_host: can't write %s\n", name); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (size_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void cv_rgb(const uint8_t *px, uint8_t *rgb) {
    for (int i = 0; i < STCV_W * STCV_H; i++) {
        uint32_t c = stcv_tms_rgb[px[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
}

// ---- the vectors of the picture shown, kept, and drawn as they are -------------------------
static st_seg_t kept[ST_SEGS];
static int nkept;
static uint32_t kept_frame;

// 512 x 384 (MAME's 1,024 x 833 at 4:3), each vector a pixel wide in its RGB
static void vec_rgb(uint8_t *rgb) {
    memset(rgb, 0, 512 * 384 * 3);
    for (int i = 0; i < nkept; i++) {
        const st_seg_t *s = &kept[i];
        const uint8_t c[3] = {(uint8_t)((s->colour >> 4 & 3) * 85), (uint8_t)((s->colour >> 2 & 3) * 85), (uint8_t)((s->colour & 3) * 85)};
        int x0 = s->x0 / 2, y0 = 383 - s->y0 * 384 / 833, x1 = s->x1 / 2, y1 = 383 - s->y1 * 384 / 833;
        int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            if (x0 >= 0 && x0 < 512 && y0 >= 0 && y0 < 384) memcpy(rgb + (y0 * 512 + x0) * 3, c, 3);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

// the vectors and the console's picture (doubled), side by side on grey
static void pair_png(const char *name, const uint8_t *v, const uint8_t *c) {
    enum { W = 512, H = 384, PW = W * 2 + 16 };
    static uint8_t out[PW * H * 3];
    memset(out, 0x30, sizeof out);
    for (int y = 0; y < H; y++) {
        memcpy(out + (size_t)y * PW * 3, v + (size_t)y * W * 3, W * 3);
        for (int x = 0; x < W; x++) memcpy(out + ((size_t)y * PW + W + 16 + x) * 3, c + ((y / 2) * STCV_W + x / 2) * 3, 3);
    }
    png(name, PW, H, out, 1);
}

// ---- the inputs at each VBLANK ---------------------------------------------------------------
static struct { int frame; char name[16]; int v; } ev[65536];
static int nev;
static bool bot;
static strun_t run;

static void apply(const char *name, int v) {        // (st_host's)
    static const struct { const char *name; int port; uint8_t mask; } F[] = {
        {"coin1", 0, 0x01}, {"coin2", 0, 0x10}, {"service", 1, 0x01}, {"start1", 2, 0x01}, {"start2", 2, 0x02},
        {"b2", 2, 0x04}, {"b1", 2, 0x08}, {"b3", 2, 0x10}, {"b4", 2, 0x20}};
    if (!strcmp(name, "dial")) { st_in.dial = (int8_t)v; return; }
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        if (strcmp(name, F[i].name)) continue;
        uint8_t *p = F[i].port == 0 ? &st_in.d7d6 : F[i].port == 1 ? &st_in.d5d4 : &st_in.fc;
        bool bit = F[i].port < 2 ? !v : v != 0;
        *p = (uint8_t)(bit ? *p | F[i].mask : *p & ~F[i].mask);
    }
}

static int bot_value(const char *name, int n) {     // (st_host's)
    static const int coin_at[] = {0, 40, 80};
    if (!strcmp(name, "coin1")) {
        for (int i = 1; i < 3; i++) if (n == coin_at[i]) return 1; else if (n == coin_at[i] + 4) return 0;
        return -1;
    }
    if (!strcmp(name, "start1")) return n == 140 ? 1 : n == 144 ? 0 : -1;
    if (n < 200) return -1;
    int t = n - 200;
    if (!strcmp(name, "dial")) {
        static const int8_t turn[8] = {3, 0, -5, 2, 0, 8, -2, -9};
        return turn[(t / 37) % 8];
    }
    if (!strcmp(name, "b1")) return (t % 9) < 3;
    if (!strcmp(name, "b2")) return (t % 150) < 4;
    if (!strcmp(name, "b3")) return (t % 400) < 90;
    if (!strcmp(name, "b4")) return (t % 1300) > 1250;
    return -1;
}

static void on_vblank(void) {
    const uint32_t before = run.pictures;
    strun_vblank();
    if (run.pictures != before) {       // the vectors that picture was made from
        memcpy(kept, st_segs, sizeof(st_seg_t) * st_nsegs);
        nkept = st_nsegs;
        kept_frame = st_frames;
    }
    const int n = (int)st_frames;
    for (int i = 0; i < nev; i++)
        if (ev[i].frame == n) apply(ev[i].name, ev[i].v);
    if (bot) {
        static const char *const names[] = {"coin1", "start1", "dial", "b1", "b2", "b3", "b4"};
        static int last[7] = {-1, -1, -1, -1, -1, -1, -1};
        for (int k = 0; k < 7; k++) {
            int v = bot_value(names[k], n);
            if (v < 0 || v == last[k]) continue;
            last[k] = v;
            apply(names[k], v);
        }
    }
}

// ---- the lists' sound (the DAC's levels) ---------------------------------------------------
static int a0 = 15, a1 = 15, a2 = 15;
static int list_samples(const uint8_t *l, size_t n, int16_t *out) {
    int k = 0;
    size_t i = 0;
    bool vblank = true;
    while (i < n) {
        uint8_t c = l[i];
        if (c == 1) i += 4 + l[i + 3];
        else if (c == 2) {
            int m = l[i + 1];
            bool got = false;
            for (int j = 0; j < m; j++) {
                uint8_t b = l[i + 2 + j];
                if ((b & 0xF0) == 0x90) { a0 = b & 15; got = true; }
                if ((b & 0xF0) == 0xB0) { a1 = b & 15; got = true; }
                if ((b & 0xF0) == 0xD0) { a2 = b & 15; got = true; }
            }
            if (got && k < STCV_PCM_N) out[k++] = (int16_t)((stcv_dac_level(a0, a1, a2) - 1.20f) * 12500.0f);
            i += 2 + (size_t)m;
        } else if (c == 3 && vblank) i += 3;
        else if (c == 0 && vblank) { vblank = false; i++; }
        else break;
    }
    return k;
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
    uint32_t v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
}

static bool in_list(const char *at, int l) {
    char key[16];
    snprintf(key, sizeof key, "%d", l);
    for (const char *s = at; *s;) {
        const char *e = strchr(s, ',');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == strlen(key) && !strncmp(s, key, len)) return true;
        s = e ? e + 1 : s + len;
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cart_host ROMDIR [options]\n"); return 2; }
    const char *dir = argv[1], *script = NULL, *pngdir = NULL, *at = "", *wavname = NULL, *statsname = NULL,
               *outname = NULL;
    int lists = 2400, from = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-lists")) lists = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-script")) script = argv[++i];
        else if (!strcmp(argv[i], "-bot")) bot = true;
        else if (!strcmp(argv[i], "-png")) pngdir = argv[++i];
        else if (!strcmp(argv[i], "-at")) at = argv[++i];
        else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
        else if (!strcmp(argv[i], "-stats")) statsname = argv[++i];
        else if (!strcmp(argv[i], "-out")) outname = argv[++i];
        else if (!strcmp(argv[i], "-from")) from = atoi(argv[++i]);
        else { fprintf(stderr, "cart_host: what is %s?\n", argv[i]); return 2; }
    }
    load(dir, "1873.cpu-u25", maincpu, 0x800);
    for (int i = 0; i < 23; i++) {
        char name[32];
        snprintf(name, sizeof name, "%d.prom-u%d", 1848 + i, 1 + i);
        load(dir, name, maincpu + 0x800 * (i + 1), 0x800);
    }
    load(dir, "1607.speech-u7", speech_cpu, 0x800);
    load(dir, "1871.speech-u6", speech_data, 0x1000);
    load(dir, "1872.speech-u5", speech_data + 0x1000, 0x1000);
    load(dir, "s-c.xyt-u39", sine, 0x400);
    if (script) {
        FILE *f = fopen(script, "r");
        if (!f) { fprintf(stderr, "cart_host: can't read %s\n", script); return 1; }
        char line[256];
        while (fgets(line, sizeof line, f) && nev < 65536)
            if (sscanf(line, "%d %15s %d", &ev[nev].frame, ev[nev].name, &ev[nev].v) == 3) nev++;
        fclose(f);
    }
    static const st_roms_t roms = {maincpu, speech_cpu, speech_data, sine};
    static stcv_t cv;
    st_in.d3d2 = ST_D3D2;
    st_in.d1d0 = ST_D1D0;
    stcv_init(&cv);
    st_init(&roms);
    strun_start(&run, &cv);
    st_on_vblank = on_vblank;

    FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
    const uint32_t rate = (uint32_t)((uint64_t)STCV_PCM_N * STCV_CONSOLE_HZ / STCV_LIST_T);
    if (wav) wav_header(wav, rate, 0);
    uint32_t nsamples = 0;
    FILE *stats = statsname ? fopen(statsname, "w") : NULL;
    FILE *out = outname ? fopen(outname, "wb") : NULL;
    static uint8_t list[STCV_LIST_MAX], px[STCV_W * STCV_H], crgb[STCV_W * STCV_H * 3], vrgb[512 * 384 * 3];
    uint64_t bytes_sum = 0;
    uint32_t len_max = 0, pics0 = 0, sec_pics = 0, worst_sec = 1000, best_sec = 0, secs = 0;
    for (int l = 1; l <= lists; l++) {
        uint32_t t0 = cv.stats.pat_bytes + cv.stats.col_bytes;
        size_t n = strun_list(&run, list);
        uint32_t t1 = cv.stats.pat_bytes + cv.stats.col_bytes;
        bytes_sum += t1 - t0;
        if (n > len_max) len_max = (uint32_t)n;
        if (wav) {
            int16_t s[STCV_PCM_N];
            int k = list_samples(list, n, s);
            fwrite(s, 2, (size_t)k, wav);
            nsamples += (uint32_t)k;
        }
        if (out && l >= from) {
            const uint8_t h[9] = {'F', 0, 0, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, 0};
            fwrite(h, 1, 9, out);
            fwrite(list, 1, n, out);
        }
        if (stats) fprintf(stats, "%d %zu %u %u %u %u\n", l, n, t1 - t0, run.pictures, cv.dirty ? 1 : 0, st_frames);
        if (l % 60 == 0) {              // pictures a second (60 lists: about one)
            uint32_t p = run.pictures - pics0;
            pics0 = run.pictures;
            if (l > 120) {
                sec_pics += p;
                secs++;
                if (p < worst_sec) worst_sec = p;
                if (p > best_sec) best_sec = p;
            }
        }
        if (pngdir && in_list(at, l)) {
            char name[1024];
            stcv_screen(&cv, px);
            cv_rgb(px, crgb);
            snprintf(name, sizeof name, "%s/cv_%d.png", pngdir, l);
            png(name, STCV_W, STCV_H, crgb, 2);
            vec_rgb(vrgb);
            snprintf(name, sizeof name, "%s/vec_%d.png", pngdir, l);
            png(name, 512, 384, vrgb, 1);
            snprintf(name, sizeof name, "%s/pair_%d.png", pngdir, l);
            pair_png(name, vrgb, crgb);
            printf("list %d: the picture of frame %u (%d vectors), backlog %u bytes\n", l, kept_frame, nkept, stcv_backlog(&cv));
        }
    }
    if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, rate, nsamples); fclose(wav); }
    if (stats) fclose(stats);
    if (out) fclose(out);
    const stcv_stats_t *st = &cv.stats;
    printf("cart_host: %d lists, %u pictures of %u frames (%.1f a second; seconds %u to %u), waited %u\n", lists,
           run.pictures, st_frames, secs ? (double)sec_pics / secs * 59.62 / 60.0 : 0.0, worst_sec, best_sec, run.waited);
    printf("  bytes %llu (pattern %u, colour %u, refreshed %u), %.0f a list, %.0f a picture; runs %u; longest list %u bytes, %u T\n",
           (unsigned long long)bytes_sum, st->pat_bytes, st->col_bytes, st->refreshed, (double)bytes_sum / lists,
           run.pictures ? (double)bytes_sum / run.pictures : 0.0, st->runs, len_max, st->t_max);
    printf("  settled %u lists, vectors %u (%u in the scanner's symbols, drawn bigger), clipped samples %u\n", st->settled,
           st->lines, st->magnified, st->clipped);
    return 0;
}
