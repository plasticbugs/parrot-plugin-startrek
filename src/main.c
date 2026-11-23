// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// Star Trek: Sega's 1982 Star Trek arcade game, from your own ROMs.
//
// Made by tools/new-plugin.py, with USB game pads and a save file on the card.
// It builds and runs as it is, with each feature on the TV, working. YOUR CODE
// marks where yours goes; take out what you don't want to show.
// docs/features.md has the features, sdk/firmware/revc/sdk/README.md the SDK.
//
// How a plugin runs. The cart has two cores:
//
//   core 1  is the cartridge: it answers every read the console makes,
//           from a small Z80 program (the SDK's frame kernel) that runs one
//           list of video and sound writes every TV frame and reports the
//           controller. cvf_start() starts it; you never touch it again.
//   core 0  is this file: one loop that never waits for long.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cvdiag.h"             // a crash becomes a note on the menu's bottom line
#include "cvframes.h"           // the console: cvf_*
#include "font8.h"              // the menu's 8x8 font, ASCII 32-95
#include "gbkernel.h"           // made by the build: the frame kernel
#include "handoff.h"            // made by the build: the ROM that parks the console between firmwares
#include "cvinput.h"            // player 1: the console's controller and USB pad 1
#include "cvusb.h"              // the USB port: pads
#include "cvsd.h"               // the card: cvsd_*, and fsro.h's files and folders

#define NAME "Star Trek"
#define BY   "Parrot Cart"

// ---- the screen: 24 rows of 28 characters -------------------------------------------------
// Each thing shown has its row. say() fills a row; picture() shows the rows.
// The font has capitals, digits and ! # & ' ( ) * + , - . / : ? (the rest
// show as ?).
enum {
    ROW_TITLE = 1, ROW_BY = 2, ROW_STICK = 4, ROW_KEYPAD = 5, ROW_USB = 7, ROW_PAD = 9,
    ROW_SAVE = 16, ROW_RESET = 22,
};
#define ROWS 24
#define COLS 28                 // (the TV cuts the edges: 2 characters each side are left off)
static char text[ROWS][COLS + 1];
// (this picture is in one color; these are the console's numbers for them)
enum { C_BLACK = 1, C_LGREEN = 3, C_LRED = 9, C_LYELLOW = 11, C_GRAY = 14, C_WHITE = 15 };

static void __attribute__((format(printf, 3, 4))) say(int row, uint8_t color, const char *fmt, ...) {
    char s[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    int i = 0;
    for (; i < COLS && s[i]; i++) text[row][i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];   // the font is upper case
    text[row][i] = 0;
    (void)color;
}

static void leave_note(const char *s) {
    cvf_note(s);                // the menu's bottom line when RESET takes the console back (32 characters at most)
}

// ---- player 1 ---------------------------------------------------------------------------
static cvin_t in;
static unsigned presses;

static void player_step(void) {
    cvin_update(&in);
    const uint8_t pressed = cvin_pressed(&in);
    const int key = cvin_key(&in);
    // YOUR CODE: player 1. in.held (CVIN_UP, CVIN_FIRE_L ...) and in.key
    // (0-9, CVIN_STAR, CVIN_HASH, -1) are what is down now; pressed and key
    // what went down since the last step.
    if (pressed || key >= 0) {
        presses++;
        static char note[33];
        snprintf(note, sizeof note, "Star Trek: %u presses", presses);
        leave_note(note);
    }
    const uint8_t h = in.held;
    say(ROW_STICK, C_WHITE, "STICK %s%s%s%s FIRE %s%s", h & CVIN_UP ? "UP " : "", h & CVIN_DOWN ? "DOWN " : "",
        h & CVIN_LEFT ? "LEFT " : "", h & CVIN_RIGHT ? "RIGHT " : "", h & CVIN_FIRE_L ? "L " : "", h & CVIN_FIRE_R ? "R" : "");
    const char k = in.key < 0 ? '-' : in.key == CVIN_STAR ? '*' : in.key == CVIN_HASH ? '#' : (char)('0' + in.key);
    say(ROW_KEYPAD, C_WHITE, "KEYPAD %c  PRESSES %u", k, presses);
}

// ---- the USB port -----------------------------------------------------------------------
static void usb_step(void) {
    const cvusb_mode_t mode = cvusb_mode();
    say(ROW_USB, mode == CVUSB_HOST || mode == CVUSB_DEVICE ? C_WHITE : C_LYELLOW, "USB %s", cvusb_mode_name(mode));
}

// ---- USB game pads ----------------------------------------------------------------------
// Buttons by where they are: S the bottom face button (B on a Nintendo pad),
// E the right one, W the left, N the top.
static const char *const BUTTON[GP_BUTTONS] = {"UP", "DN", "LT", "RT", "S", "E", "W", "N", "L", "R",
                                               "ZL", "ZR", "-", "+", "HOME", "CAP", "L3", "R3"};

static void pads_step(void) {
    for (int n = 0; n < CVUSB_PAD_MAX; n++) {
        const int row = ROW_PAD + 3 * n;
        const char *kind = cvusb_pad_kind(n);
        gamepad_t p;
        if (!cvusb_pad(n, &p)) {
            say(row, C_GRAY, "PAD %d %s", n + 1, kind ? "STARTING" : "-");
            say(row + 1, C_GRAY, "%s", "");
            say(row + 2, C_GRAY, "%s", "");
            continue;
        }
        // YOUR CODE: pad n. p.buttons: the GP_* held (gamepad.h); p.lx, p.ly,
        // p.rx, p.ry: the sticks, -2048..2047, y up. Pad 1 is player 1 too.
        char b[64] = "";
        size_t at = 0;
        for (int i = 0; i < GP_BUTTONS; i++)
            if (p.buttons & 1u << i) at += (size_t)snprintf(b + at, sizeof b - at, "%s ", BUTTON[i]);
        say(row, C_WHITE, "PAD %d %s", n + 1, kind ? kind : "");
        say(row + 1, C_WHITE, "  %s", at ? b : "-");
        say(row + 2, C_WHITE, "  L%+5d%+5d R%+5d%+5d", p.lx, p.ly, p.rx, p.ry);
    }
}

// ---- the save file ----------------------------------------------------------------------
// SAVE_FILE in SAVE_FOLDER, 512 bytes, found or made at the start. Its
// layout is yours. Keep SAVE_MAGIC at its start, and change its last
// character when the layout changes, so a new release can tell an old save.
#define SAVE_FOLDER "/STARTREK"
#define SAVE_FILE   "STARTREK.SAV"
#define SAVE_MAGIC  "STARTRE1"
typedef struct {
    char magic[8];
    uint32_t runs;              // how many times the plugin has started
    // YOUR CODE: what your plugin keeps
} save_t;
static save_t save;
static fs_t fs;                 // the file system's structures are big (an open file's map is 12K): static
static fs_file_t save_f;
static bool save_ok;
static uint8_t sector[512];

// Write `save` to the card, in place. A write takes milliseconds: call it
// when there is something new to keep, not every step.
static bool save_write(void) {
    if (!save_ok) return false;
    memset(sector, 0, sizeof sector);
    memcpy(sector, &save, sizeof save);
    return fs_write_sec(&fs, 0, sector) == FS_OK;
}

// Once, at the start: the card's start takes a second or so, with the
// screen standing still.
static void save_start(void) {
    const char *why = NULL;
    if (!cvsd_card_in()) {
        why = "NO CARD";
    } else if (!cvsd_init()) {
        why = "THE CARD DIDN'T START";
    } else if (fs_mount(&fs, cvsd_read) != FS_OK) {
        why = "NO FAT OR EXFAT ON THE CARD";
    } else {
        fs_set_writer(&fs, cvsd_write);                     // without this the card is read-only
        uint16_t name[16];                                  // the file system takes names as UTF-16
        int n = 0;
        for (; SAVE_FILE[n]; n++) name[n] = (uint8_t)SAVE_FILE[n];
        bool made;
        const int r = fs_create(&fs, SAVE_FOLDER, name, n, 512, &save_f, &made);   // found, or made with its size
        if (r == FS_ENOFILE) {
            why = "NO FOLDER " SAVE_FOLDER;
        } else if (r != FS_OK || fs_open(&fs, &save_f) != FS_OK) {
            why = "THE SAVE WON'T OPEN";
        } else {
            memcpy(save.magic, SAVE_MAGIC, 8);
            if (!made && cvsd_read(fs_lba(&fs, 0), sector) && !memcmp(sector, SAVE_MAGIC, 8))
                memcpy(&save, sector, sizeof save);         // it was there, and it is ours
            save_ok = true;
            save.runs++;
            if (!save_write()) why = "THE SAVE DIDN'T WRITE";
        }
    }
    if (why) say(ROW_SAVE, C_LRED, "SAVE: %s", why);
    else say(ROW_SAVE, C_WHITE, "SAVE: RUN %lu, %s", (unsigned long)save.runs, SAVE_FILE);
}

// ---- the step: every TV frame -----------------------------------------------------------
static void step(void) {
    player_step();
    usb_step();
    pads_step();
    // YOUR CODE: your plugin's step
}

// ---- the picture ------------------------------------------------------------------------
// The frame kernel sets up Graphics II: 768 names at $1800 say which tile
// is at each 8x8 place, and each third of the screen has its own 256 tiles:
// patterns at $0000, colors at $2000, 8 bytes a tile. Here tiles 32-95 are
// the font, in each third, and a place's name is its character.
//
// want[] is the video memory the plugin wants and shown[] what the console
// will hold once the lists sent so far have run. Each frame's list carries
// as many of the bytes that differ as fit in its time (examples/frames).
#define NAMES    0x1800
#define SPRITES  0x1B00         // (the sprites' tables, to $1FFF: not written)
#define COLORS   0x2000
#define VRAM_END 0x3800
static uint8_t want[VRAM_END], shown[VRAM_END];

static void picture_start(void) {
    for (int third = 0; third < 3; third++)
        for (int ch = 32; ch < 96; ch++)
            for (int r = 0; r < 8; r++) {
                const int i = third * 0x800 + ch * 8 + r;
                want[i] = font8[ch - 32][r];
                want[COLORS + i] = 0xF0 | C_BLACK;      // white on black
                shown[i] = (uint8_t)~want[i];           // what the console has there isn't known: send it
                shown[COLORS + i] = (uint8_t)~want[COLORS + i];
            }
    memset(shown + NAMES, 0xFF, 768);
}

// The whole picture, every frame
static void picture(void) {
    // YOUR CODE: your picture. This one is the rows of text.
    for (int r = 0; r < ROWS; r++) {
        const int n = (int)strlen(text[r]);
        for (int col = 0; col < 32; col++) {
            const int i = col - 2;                      // two places each side: the TV cuts the edges
            const char ch = i >= 0 && i < n ? text[r][i] : ' ';
            want[NAMES + 32 * r + col] = (uint8_t)(ch >= 32 && ch < 96 ? ch : '?');
        }
    }
}

// What each command costs in Z80 cycles (gbkernel.asm's header)
#define FRAME_T     (262 * 228)
#define BLANK_T     (70 * 228)
#define BLANK_LIMIT (BLANK_T - 2000)    // the first part must end inside the blank...
#define LIST_LIMIT  (FRAME_T - 4000)    // ...and the list well inside the frame
#define RUN_T       106
#define PSG_T(n)    (75 + 52 * (n))     // with the ~29 T the sound chip holds the Z80 for at each byte
#define END_T       54
static uint8_t list[CVF_LIST_MAX];
static size_t len;
static uint32_t cost, cursor;

// The bytes of want[] that differ from shown[], as runs, until `limit`
// cycles are used up at per_byte cycles a byte. Everything in a list must
// be safe to run twice: with nothing new, the console runs the last again.
static void diff(uint32_t limit, uint32_t per_byte) {
    for (uint32_t scanned = 0; scanned < VRAM_END;) {
        if (cursor == SPRITES) cursor = COLORS;
        const uint32_t i = cursor, end = i < NAMES ? NAMES : i < SPRITES ? SPRITES : VRAM_END;
        if (want[i] == shown[i]) {
            cursor = (cursor + 1) % VRAM_END;
            scanned++;
            continue;
        }
        if (cost + RUN_T + per_byte > limit) return;
        // a run from here: up to 255 bytes, to the table's end, while the
        // time lasts, stopping at 4 bytes in a row that are right already
        uint32_t n = 0, same = 0;
        while (n < 255 && i + n < end && cost + RUN_T + per_byte * (n + 1) <= limit) {
            same = want[i + n] == shown[i + n] ? same + 1 : 0;
            if (same == 4) break;
            n++;
        }
        n -= same == 4 ? 3 : same;                      // not the right ones at its end
        list[len++] = 1;
        list[len++] = (uint8_t)i;
        list[len++] = (uint8_t)(i >> 8 | 0x40);
        list[len++] = (uint8_t)n;
        memcpy(list + len, want + i, n);
        memcpy(shown + i, want + i, n);                 // it will be so, once this list has run
        len += n;
        cost += RUN_T + per_byte * n;
        cursor = (i + n) % VRAM_END;
        scanned += n;
    }
}

// ---- the sound ----------------------------------------------------------------------------
// The sound chip's ten bytes: tone low, tone high and volume for voices 0
// to 2, then the noise's volume (a volume's low 4 bits: 0 loudest, 15 off).
// Every list carries all of them, changed or not. Now and then the console
// reads a byte from the cart wrong, and a register written only once would
// keep the wrong value, a drone, until RESET.
static uint8_t psg[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};   // all off

static void sound(void) {
    // YOUR CODE: your sound, into psg[]. Make it about as loud as an
    // ordinary ColecoVision cartridge (docs/testing.md, "How loud").
    list[len++] = 2;
    list[len++] = sizeof psg;
    memcpy(list + len, psg, sizeof psg);
    len += sizeof psg;
    cost += PSG_T(sizeof psg);
}

// ---- the start, and the loop ------------------------------------------------------------
static void rows_start(void) {
    say(ROW_TITLE, C_LYELLOW, "%s", NAME);
    say(ROW_BY, C_GRAY, "BY %s", BY);
    say(ROW_RESET, C_GRAY, "RESET: BACK TO THE MENU");
}

int main(void) {
    // First thing: the console is parked, waiting for a cartridge to appear.
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    picture_start();
    cvin_init(&in);
    leave_note(NAME);
    rows_start();
    save_start();
    cvusb_start();              // after the kernel's start: a host, for what is plugged into the hub

    // YOUR CODE: set up your plugin here

    bool pending = false;       // a list is made and not taken yet
    uint32_t seen = 0;
    for (;;) {
        const uint32_t t = time_us_32();
        // Every pass: is the console still on, has RESET been pressed? On
        // RESET this doesn't return: the cart goes back to the menu.
        cvf_task(t);
        cvusb_task();           // every pass: the USB port's work (it never waits)
        // Never drop a list: shown[] already counts on it. Offer it until it is taken.
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();                            // a TV frame has started: make the list for the next
        uint8_t joy, fire, key;
        cvf_input(&joy, &fire, &key);                   // the controller, as the console read it this frame
        cvin_frame(&in, joy, fire, key);
        step();
        picture();
        len = 0;
        cost = 480;                                     // the kernel's own start of a frame
        sound();                                        // the whole sound chip, every list
        diff(BLANK_LIMIT, 23);                          // the blank part: 23 cycles a byte
        list[len++] = 0;
        cost += END_T;
        diff(LIST_LIMIT, 31);                           // while the picture is drawn: 31
        list[len++] = 0;
        pending = true;
    }
}
