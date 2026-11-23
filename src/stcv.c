// stcv.c -- see stcv.h. The lists and the DAC are Time Pilot's (Parrot
// Cart's timeplt/tpcv.c, from Donkey Kong's, Pac-Man's and Q*bert's); the
// line drawing is Star Wars' (swcv.c); the scanner's magnified symbols and
// the colours are new.
#include "stcv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "stdac3.h"

// ---- lists (from gbkernel.asm's costs) -----------------------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// The console goes from one list straight into the next, so a list's
// length is the sample clock
#define C_FRAME    357
#define PCM_L      STCV_LIST_T
#define PCM_FIRST  (C_FRAME + C_END_VB)
#define C_SAMPLE   (C_PSG + 3 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (STCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLORS   0x2000
#define NAMES    0x1800
#define BLACK    1

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- colours ------------------------------------------------------------------------
const uint32_t stcv_tms_rgb[16] = {0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
                                   0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF};
uint8_t stcv_colour[64];
static uint8_t colour_rank[64];         // 1 the greys (the stars and the dust), 2 the rest
static bool colours_ready;

static void oklab(uint32_t rgb, float *out) {
    float c[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((rgb >> (16 - 8 * i)) & 255) / 255.0f;
        c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    float l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
    float m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
    float s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
    out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

// The board's colours (MAME's color222: each gun 0, 85, 170 or 255) as
// the nearest TMS9918 colour in OKLab, never black (a vector is always
// seen); a grey to grey or white only (the dust and the stars)
static void colours_init(void) {
    float tms[16][3];
    for (int i = 2; i < 16; i++) oklab(stcv_tms_rgb[i], tms[i]);
    for (int c = 0; c < 64; c++) {
        int r = c >> 4 & 3, g = c >> 2 & 3, b = c & 3;
        colour_rank[c] = (r == g && g == b) ? 1 : 2;
        if (r == g && g == b) {
            stcv_colour[c] = r == 3 ? 15 : 14;
            continue;
        }
        float lab[3];
        oklab((uint32_t)(r * 85) << 16 | (uint32_t)(g * 85) << 8 | (uint32_t)(b * 85), lab);
        float bd = 1e9f;
        uint8_t best = 15;
        for (int t = 2; t < 16; t++) {
            if (t == 14 || t == 15) continue;           // (greys only for greys)
            float x = lab[0] - tms[t][0], y = lab[1] - tms[t][1], z = lab[2] - tms[t][2];
            float d = x * x + y * y + z * z;
            if (d < bd) { bd = d; best = (uint8_t)t; }
        }
        stcv_colour[c] = best;
    }
    // the dim olive of the screen's dividers (and the logo's inner lines)
    // is nearest dark red, the wrong hue: dark yellow, as MAME shows it
    stcv_colour[0x14] = 10;
    colours_ready = true;
}

// ---- the picture -------------------------------------------------------------------
// MAME's units (x 0-1024 across, y 0-832 up) to pixels, rounded: 256/1025
// and 192/833 in 1/65536ths
#define PX(x) (((x) * 16368 + 32768) >> 16)
#define PY(y) (STCV_H - 1 - (((y) * 15106 + 32768) >> 16))

// The scanner: right of the line at x 300 and above the one at y 416
// (MAME's units). Its symbols no more than 64 across are drawn bigger about
// where they start (their centre): those no more than 12 across (the
// Klingons, the starbases, the torpedoes) 3.5 times, the rest (the
// Enterprise in its shields) twice, inside the scanner's lines.
#define SCAN_X  300
#define SCAN_Y  416
#define SCAN_TOP 800                    // (the divider reaches 830)
#define SCAN_MAX 64
#define SCAN_SMALL 12
typedef struct { int x0, y0, x1, y1; } rect_t;
static const rect_t whole = {0, 0, STCV_W - 1, STCV_H - 1};
static rect_t scanner;

static int outcode(const rect_t *r, int x, int y) {
    return (x < r->x0 ? 1 : x > r->x1 ? 2 : 0) | (y < r->y0 ? 4 : y > r->y1 ? 8 : 0);
}

// A line, clipped to r (Cohen-Sutherland), then Bresenham's with the
// pixel's place in the bitmap kept as a byte address and a bit: a step
// across moves the bit, a step down the address (by 1 inside a place, by
// 249 to the place below) (swcv.c's). Each place takes the line's colour
// unless it has one of a higher rank already.
static STCV_HOT void line(stcv_t *e, const rect_t *r, int x0, int y0, int x1, int y1, uint8_t col, uint8_t rank) {
    int c0 = outcode(r, x0, y0), c1 = outcode(r, x1, y1);
    for (int i = 0; i < 8 && (c0 | c1); i++) {
        if (c0 & c1) return;
        int c = c0 ? c0 : c1, x, y;
        if (c & 8) { x = x0 + (x1 - x0) * (r->y1 - y0) / (y1 - y0); y = r->y1; }
        else if (c & 4) { x = x0 + (x1 - x0) * (r->y0 - y0) / (y1 - y0); y = r->y0; }
        else if (c & 2) { y = y0 + (y1 - y0) * (r->x1 - x0) / (x1 - x0); x = r->x1; }
        else { y = y0 + (y1 - y0) * (r->x0 - x0) / (x1 - x0); x = r->x0; }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(r, x0, y0); }
        else { x1 = x; y1 = y; c1 = outcode(r, x1, y1); }
    }
    if (c0 | c1) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    unsigned a = (unsigned)(((y0 >> 3) * 32 + (x0 >> 3)) * 8 + (y0 & 7));
    unsigned bit = 0x80u >> (x0 & 7);
    int n = dx > dy ? dx : dy, yin = y0 & 7;
    uint8_t *pat = e->want_pat, *cl = e->want_col, *pri = e->want_pri;
    for (;; n--) {
        pat[a] |= (uint8_t)bit;
        if (rank >= pri[a]) { cl[a] = col; pri[a] = rank; }
        if (!n) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            if (sx > 0) { bit >>= 1; if (!bit) { bit = 0x80; a += 8; } }
            else { bit <<= 1; if (bit == 0x100) { bit = 1; a -= 8; } }
        }
        if (e2 < dx) {
            err += dx;
            if (sy > 0) { if (++yin == 8) { yin = 0; a += 249; } else a++; }
            else { if (--yin < 0) { yin = 7; a -= 249; } else a--; }
        }
    }
}

// The messages ("AUTO SHIELD", "PLAYER 1") are yellow, and some go in the
// scanner's corner, along its bottom or its top: a symbol all in that
// yellow is drawn as it is (no ship there is all yellow)
#define WORDS 0x3c

// The game's screen is up: its scanner's left edge, the divider at x 300
// from y 416 to the top, is drawn in every frame of it and in no other
// (the high scores and the titles put their letters in the same corner,
// and those stay as they are)
static bool scanner_up(const st_seg_t *segs, int n) {
    for (int k = 0; k < n; k++) {
        const st_seg_t *s = &segs[k];
        if (s->x0 == SCAN_X && s->x1 == SCAN_X && (s->y0 < s->y1 ? s->y0 : s->y1) <= SCAN_Y &&
            (s->y0 > s->y1 ? s->y0 : s->y1) >= SCAN_TOP)
            return true;
    }
    return false;
}

STCV_HOT void stcv_vectors(stcv_t *e, const st_seg_t *segs, int n) {
    if (!colours_ready) colours_init();
    memset(e->want_pat, 0, sizeof e->want_pat);
    memset(e->want_pri, 0, sizeof e->want_pri);
    const bool game_screen = scanner_up(segs, n);
    for (int i = 0; i < n;) {
        // a symbol's vectors (they come together), and their extent
        int j = i, xmin = segs[i].x0, xmax = xmin, ymin = segs[i].y0, ymax = ymin;
        bool words = true;              // all in the messages' yellow: letters
        for (; j < n && segs[j].symbol == segs[i].symbol; j++) {
            const st_seg_t *s = &segs[j];
            if (s->colour != WORDS) words = false;
            if (s->x0 < xmin) xmin = s->x0;
            if (s->x0 > xmax) xmax = s->x0;
            if (s->x1 < xmin) xmin = s->x1;
            if (s->x1 > xmax) xmax = s->x1;
            if (s->y0 < ymin) ymin = s->y0;
            if (s->y0 > ymax) ymax = s->y0;
            if (s->y1 < ymin) ymin = s->y1;
            if (s->y1 > ymax) ymax = s->y1;
        }
        int size = xmax - xmin > ymax - ymin ? xmax - xmin : ymax - ymin;
        int ox = segs[i].ox, oy = segs[i].oy;
        // the magnification, in halves (2: as it is)
        int mag = (game_screen && !words && ox > SCAN_X && oy > SCAN_Y && size <= SCAN_MAX) ? (size <= SCAN_SMALL ? 7 : 4) : 2;
        const rect_t *r = mag == 2 ? &whole : &scanner;
        for (; i < j; i++) {
            const st_seg_t *s = &segs[i];
            int x0 = s->x0, y0 = s->y0, x1 = s->x1, y1 = s->y1;
            if (mag != 2) {
                x0 = ox + (x0 - ox) * mag / 2;
                y0 = oy + (y0 - oy) * mag / 2;
                x1 = ox + (x1 - ox) * mag / 2;
                y1 = oy + (y1 - oy) * mag / 2;
                e->stats.magnified++;
            }
            line(e, r, PX(x0), PY(y0), PX(x1), PY(y1), (uint8_t)(stcv_colour[s->colour] << 4 | BLACK),
                 colour_rank[s->colour]);
            e->stats.lines++;
        }
    }
    e->stats.pictures++;
    e->dirty = true;
}

void stcv_canvas(stcv_t *e, const uint8_t *px) {
    for (int y = 0; y < STCV_H; y++)
        for (int c = 0; c < 32; c++, px += 8) {
            unsigned a = (unsigned)(((y >> 3) * 32 + c) * 8 + (y & 7));
            uint8_t pat = 0, col = 15;
            for (int x = 0; x < 8; x++)
                if (px[x] > 1) { pat |= (uint8_t)(0x80 >> x); col = px[x]; }
            e->want_pat[a] = pat;
            e->want_col[a] = (uint8_t)(col << 4 | BLACK);
        }
    e->stats.pictures++;
    e->dirty = true;
}

// (a message's canvas is 256 x 192 bytes and its dirty marks; the card's
// state and reads need less)
uint8_t *stcv_scratch(size_t *n) {
    static uint8_t room[STCV_W * STCV_H + 1024] __attribute__((aligned(8)));
    *n = sizeof room;
    return room;
}

static void screen_of(const uint8_t *pat, const uint8_t *colt, uint8_t *px) {
    for (int y = 0; y < STCV_H; y++)
        for (int x = 0; x < STCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = colt[a];
            c = (pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * STCV_W + x] = c ? c : BLACK;
        }
}

void stcv_screen(const stcv_t *e, uint8_t *px) { screen_of(e->vram_pat, e->vram_col, px); }
void stcv_wanted(const stcv_t *e, uint8_t *px) { screen_of(e->want_pat, e->want_col, px); }

// ---- what the console needs ----------------------------------------------------------
// a place's pattern byte when it differs; its colour byte only where it has
// pixels (where it has none, the colour the console holds will do)
static inline bool need_pat(const stcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const stcv_t *e, unsigned j) { return e->want_pat[j] && e->want_col[j] != e->vram_col[j]; }

// The next bytes to send, from the cursor on (once round): a place's colour
// byte before its pattern byte. *m: the run's natural length, with gaps of
// up to 3 bytes that are right already taken in (a new run costs more).
// Most of the screen is right already, and is passed over a word at a
// time. (Star Wars' and Time Pilot's next_run.)
STCV_HOT static bool next_run(stcv_t *e, bool *color, unsigned *s, int *m, unsigned most) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    for (unsigned k = 0; k < STCV_BYTES;) {
        if (!(i & 3) && k + 4 <= STCV_BYTES) {
            uint32_t w, v;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            if (w == v) {
                uint32_t wc, vc;
                memcpy(&wc, e->want_col + i, 4);
                memcpy(&vc, e->vram_col + i, 4);
                if (!w || wc == vc) {
                    k += 4;
                    i = i + 4 == STCV_BYTES ? 0 : i + 4;
                    continue;
                }
            }
        }
        bool nc = need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == STCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *color = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < STCV_BYTES && j - i < most; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;
    return false;
}

static void put_run(stcv_t *e, list_t *l, bool color, unsigned s, int n) {
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (color) {
        // (a byte taken in for a place with no pixels: what the console holds)
        for (int k = 0; k < n; k++) d[k] = e->want_pat[s + k] ? e->want_col[s + k] : e->vram_col[s + k];
        memcpy(e->vram_col + s, d, n);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// what's on screen written again as it is (qbcv.c's)
static void refresh_run(stcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool color = at >= STCV_BYTES;
    unsigned s = color ? at - STCV_BYTES : at;
    if (s + n > STCV_BYTES) n = (int)(STCV_BYTES - s);
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (color ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * STCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t stcv_backlog(const stcv_t *e) {
    uint32_t n = 0;
    for (unsigned j = 0; j < STCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    return n;
}

// the name table: name i at place i in each third (it changes never)
static void names_run(stcv_t *e, list_t *l, int n) {
    int left = 768 - e->names_at;
    if (n > left) n = left;
    l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
    for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
    e->names_at = (uint16_t)(e->names_at + n);
    if (e->names_at == 768) e->names_done = true;
}

// ---- a list, the console waiting for the vertical blank (messages) ----------------------
static void plain_list(stcv_t *e, list_t *l) {
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else if (e->tone_n) {
        l_psg(l, e->tone_bytes, e->tone_n);
    }
    while (!e->names_done) {
        int n = room(l);
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) break;
        names_run(e, l, n);
    }
    bool color;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &color, &s, &m, 255)) {
        int n = room(l);
        if (n < m && l->vblank) {
            if (n >= 8) put_run(e, l, color, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, color, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a list with samples in it (qbcv.c's pcm_list) -------------------------------------
STCV_HOT static void fill(stcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    if (!e->names_done) { names_run(e, l, n); return; }
    bool color;
    unsigned s;
    int m;
    if (!next_run(e, &color, &s, &m, 1)) {
        if (e->refresh_at % STCV_BYTES + (unsigned)n > STCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / STCV_BYTES * STCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > STCV_BYTES) s = STCV_BYTES - (unsigned)n;
    put_run(e, l, color, s, n);
}

STCV_HOT static void pcm_list(stcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < STCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        for (int j = 0; j < 3; j++) b[n++] = e->pcm_bytes[k][j];
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == STCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        // a short run of the picture's as one of two (qbcv.c's)
        bool color;
        unsigned s;
        int m = 0;
        bool two = e->names_done && next_run(e, &color, &s, &m, 7) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int need = two ? m + 1 : 1;
        int base = avail - (two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * need; r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][2];     // (the last attenuator again: no change)
        l_psg(l, b, n);
        if (two) put_run(e, l, color, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t stcv_tv_frame(stcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound: samples through the DAC ------------------------------------------------
// The DAC: the three tones at a period of 1, their attenuators as measured
// on a console (tools/ladder.json), summed. stdac3.h's 512 levels go from
// 0 to 2.40, each a triple of attenuators chosen so a small change makes
// small partial sums between a sample's three writes (tools/mkdac3.py).
// The sound rests at the middle (DAC_MID). stsnd.c hands over samples
// already scaled to the DAC's levels either side of the middle (+-32767
// for +-511 levels); a soft knee from 3/4 of the way rounds the loudest
// off, never clipping.
#define DAC_LEVELS STDAC3_LEVELS
#define DAC_MID    255
#define DAC_HALF   255.0f
#define DAC_KNEE   191.0f
static const float ladder[3][16] = {
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t limit[512];                  // |level| (0-511) to 0-255: straight to DAC_KNEE, then soft
static int16_t lq[3][16];                   // ladder in 1/16384ths, for put_sample's integer sums
static bool dac_ready;

float stcv_dac_level(int a0, int a1, int a2) { return ladder[0][a0 & 15] + ladder[1][a1 & 15] + ladder[2][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < 512; i++) {
        float v = (float)i;
        if (v > DAC_KNEE) v = DAC_KNEE + (DAC_HALF - DAC_KNEE) * (1.0f - expf(-(v - DAC_KNEE) / (DAC_HALF - DAC_KNEE)));
        limit[i] = (uint8_t)(v + 0.5f);
    }
    for (int c = 0; c < 3; c++)
        for (int a = 0; a < 16; a++) lq[c][a] = (int16_t)lrintf(ladder[c][a] * 16384.0f);
    dac_ready = true;
}

// a sample's three writes, in the order whose two partial sums (the chip
// between the writes, 52 T each) are nearest the new level, in integers
// (dkcv.c's put_sample)
static const uint8_t order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static inline void put_sample(uint8_t *out, int *a0, int *a1, int *a2, uint16_t t) {
    const int w0 = t >> 8, w1 = t >> 4 & 15, w2 = t & 15;
    const int32_t d0 = lq[0][w0] - lq[0][*a0], d1 = lq[1][w1] - lq[1][*a1], d2 = lq[2][w2] - lq[2][*a2];
    const int32_t sum = d0 + d1 + d2;
    const int32_t f0 = iabs(sum - d0), f1 = iabs(sum - d1), f2 = iabs(sum - d2);
    const int32_t l0 = iabs(d0), l1 = iabs(d1), l2 = iabs(d2);
    int32_t best = f0 + l2, v;
    int pick = 0;
    if ((v = f0 + l1) < best) { best = v; pick = 1; }
    if ((v = f1 + l2) < best) { best = v; pick = 2; }
    if ((v = f1 + l0) < best) { best = v; pick = 3; }
    if ((v = f2 + l1) < best) { best = v; pick = 4; }
    if ((v = f2 + l0) < best) pick = 5;
    const uint32_t b = (uint32_t)(0x90 | w0) | (uint32_t)(0xB0 | w1) << 8 | (uint32_t)(0xD0 | w2) << 16;
    out[0] = (uint8_t)(b >> 8 * order[pick][0]);
    out[1] = (uint8_t)(b >> 8 * order[pick][1]);
    out[2] = (uint8_t)(b >> 8 * order[pick][2]);
    *a0 = w0; *a1 = w1; *a2 = w2;
}

STCV_HOT void stcv_sound_pcm(stcv_t *e, const int16_t *s, int n) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;
    if (n <= 0) {                       // silence: the whole chip, every list (a misread byte lasts a list)
        static const uint8_t off[] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
        memcpy(e->tone_bytes, off, sizeof off);
        e->tone_n = sizeof off;
        e->pcm = false;
        return;
    }
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;     // (as the lists before left them)
    }
    // Every list: the three tones parked at a period of 1, the noise silent,
    // so every register is written each list (a byte the console misreads
    // lasts a list at most)
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xE4, 0xFF};
    memcpy(e->tone_bytes, park, sizeof park);
    e->tone_n = sizeof park;
    int32_t gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1], a2 = e->dac_att[2];
    uint32_t clipped = 0;
    for (int k = 0; k < STCV_PCM_N; k++) {
        int32_t v = s[k < n ? k : n - 1] >> 6;          // +-32767 to +-511 levels
        int32_t a = v < 0 ? -v : v;
        if (a > (int32_t)DAC_KNEE) clipped++;
        a = limit[a > 511 ? 511 : a];
        int32_t lvl = DAC_MID + (v < 0 ? -a : a);
        if (gain < 32 && !(k & 7)) gain++;              // (up to its rest over 43 ms)
        lvl = lvl * gain / 32;
        put_sample(e->pcm_bytes[k], &a0, &a1, &a2, st_dac3[lvl < 0 ? 0 : lvl > DAC_LEVELS - 1 ? DAC_LEVELS - 1 : lvl]);
    }
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
    e->dac_att[2] = (uint8_t)a2;
    e->stats.clipped += clipped;
}

void stcv_init(stcv_t *e) {
    memset(e, 0, sizeof *e);
    e->dirty = true;
    if (!colours_ready) colours_init();
    if (!dac_ready) dac_init();
    scanner = (rect_t){PX(SCAN_X) + 1, 0, STCV_W - 1, PY(SCAN_Y) - 1};
}
