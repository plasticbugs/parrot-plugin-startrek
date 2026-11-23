// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Parrot Cart
//
// Star Trek -- Sega's 1982 vector arcade game (Star Trek: Strategic
// Operations Simulator) on the ColecoVision: the cart runs the arcade board
// (st.c, MAME 0.288's emulation: the main Z80, the Universal Sound Board's
// 8035 and the speech board's 8035 with its SP0250) from the user's ROMs on
// the card, and stcv.c draws its vectors on the console's screen and sends
// them with the sound (stsnd.c) as the frame kernel's lists. RESET goes
// back to the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  this loop: a list at a time (strun.c), the board run for the
//           list's time, the changes to the newest picture and the list's
//           samples, handed to core 1 as soon as it will take it
//
// The console runs the lists back to back (their sound is a sample
// stream), and each list runs the board for just the list's time (16.77
// ms), so the game keeps the arcade's speed. A list that isn't ready when
// the console wants it makes the console run the last one again (its sound
// once more) and puts the game a list behind. The note counts those.
//
// The ROMs (MAME's "startrek" set) are read from the card's /STARTREK
// folder, loose or zipped, or from /STARTREK.ZIP, each known by its size
// and CRC-32.
//
// Controls (the kit's arcade keys, and the arcade's four buttons): the
// stick left and right, or a USB pad's d-pad or left stick, turns the ship
// (the arcade's spinner); the left fire button fires phasers, the right
// one photon torpedoes; the stick up is impulse, down is warp. On a pad: A
// (the bottom face button) phasers, B (the right one) photon torpedoes, X
// (the left one) impulse, Y (the top one) warp, and up and down as the
// stick. Keypad * or #, or -, is a coin; keypad 1 or + starts one player,
// keypad 2 two.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#if __has_include("hardware/watchdog.h")
#include "hardware/watchdog.h"
#else                                   // (the kit's model of the console, test/host: no watchdog)
static inline void watchdog_enable(uint32_t ms, bool pause) { (void)ms; (void)pause; }
static inline void watchdog_update(void) {}
#endif

#include "cvdiag.h"
#include "cvframes.h"
#include "cvinput.h"
#include "cvsd.h"
#include "cvusb.h"
#include "gbkernel.h"
#include "handoff.h"
#include "vdp.h"
#include "st.h"
#include "stcard.h"
#include "stcv.h"
#include "strun.h"

#define OPEN_US     20000           // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS 4000            // a hang reboots into the menu
#define NOTE_US     1000000         // the menu's note is brought up to date this often
#define GAME        "Star Trek"     // in the menu's notes
#define TAG         "ST"

static stcv_t enc;
static strun_t run;
static st_roms_t roms;
static uint8_t list[STCV_LIST_MAX];
static size_t list_len;
static bool list_pending;                   // built, not published yet
static uint32_t seen_frames;
static uint8_t *scratch;                    // room before the game: a message's canvas, the card's reads
static size_t scratch_n;

#define CANVAS      (*(canvas_t *)(void *)scratch)

// ---- the console ---------------------------------------------------------------------
static void tasks(void) {
    cvusb_task();
    cvf_task(time_us_32());
    watchdog_update();
}

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// TV frames the console has started since the last call (0: none yet, or
// the last list isn't out): for the messages' lists, a TV frame each
static uint32_t new_frames(void) {
    tasks();
    publish();
    if (list_pending) return 0;
    uint32_t f = cvf_frames(), n = f - seen_frames;
    seen_frames = f;
    return n;
}

static void send(void) {
    list_len = stcv_tv_frame(&enc, list);
    list_pending = true;
    publish();
}

// ---- messages ------------------------------------------------------------------------
static void center(int y, const char *s, uint8_t color, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    canvas_text(&CANVAS, w < VDP_W ? (VDP_W - w) / 2 : 0, y, s, color, scale);
}

static void message(const char *head, const char *l1, const char *l2, const char *l3) {
    canvas_init(&CANVAS);
    center(24, "STAR", C_LYELLOW, 3);
    center(52, "TREK", C_LYELLOW, 3);
    if (head) center(80, head, C_WHITE, 1);
    if (l1) center(100, l1, C_GRAY, 1);
    if (l2) center(112, l2, C_GRAY, 1);
    if (l3) center(124, l3, C_GRAY, 1);
    center(176, "RESET: BACK TO THE MENU", C_LBLUE, 1);
    stcv_canvas(&enc, &CANVAS.px[0][0]);
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frames()) continue;
        stcv_sound_pcm(&enc, NULL, 0);
        send();
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *l2, const char *l3, const char *menu_note) {
    message("IT CAN'T START", why, l2, l3);
    cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

// between a file's sectors: the console keeps its picture
static void card_tick(void) {
    if (!new_frames()) return;
    stcv_sound_pcm(&enc, NULL, 0);
    send();
}

// ---- the controls ----------------------------------------------------------------------
// The board's ports (st.h): D7D6 0x01 coin 1 (active low); FC (active
// high) 0x01 start 1, 0x02 start 2, 0x08 phasers, 0x04 photon torpedoes,
// 0x10 impulse, 0x20 warp; the spinner's move each game frame. A coin is a
// pulse of 6 lists from the press, and the key has to be up for 3 lists
// before the next one counts.
//
// MAME's coin flip-flop only counts a press that follows a release it has
// seen, and at power on it has seen none, so the first coin would be lost
// (the arcade's switch has been up since power on): the coin switch goes
// down and up once at the start, before the game looks at it.
static void controls(void) {
    uint8_t joy, fire, lines;
    cvf_input(&joy, &fire, &lines);                     // low = pressed
    const int key = cvin_decode_key(lines);
    uint32_t dirs = 0, buttons = 0;
    int lx = 0;
    gamepad_t p;
    if (cvusb_pad(0, &p)) {
        buttons = p.buttons;
        dirs = buttons & (GP_UP | GP_DOWN | GP_LEFT | GP_RIGHT);       // the d-pad (the stick is the spinner's)
        lx = p.lx;
        if (p.ly > 1024) dirs |= GP_UP;
        if (p.ly < -1024) dirs |= GP_DOWN;
    }
    const bool up = !(joy & 1) || (dirs & GP_UP), right = !(joy & 2) || (dirs & GP_RIGHT);
    const bool down = !(joy & 4) || (dirs & GP_DOWN), left = !(joy & 8) || (dirs & GP_LEFT);
    const bool phasers = !(fire & 1) || (buttons & GP_SOUTH);
    const bool photon = !(fire & 2) || (buttons & GP_EAST);
    const bool impulse = (up && !down) || (buttons & GP_WEST);
    const bool warp = (down && !up) || (buttons & GP_NORTH);
    const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
    const bool start1 = key == 1 || (buttons & GP_PLUS);
    const bool start2 = key == 2;

    // the spinner: the stick turns 4 a game frame at first, up to MAME's 10
    // (its key step) after a third of a second held; a pad's stick in
    // proportion to how far it's pushed, 12 at the most
    static int spin_held;
    int spin = 0;
    if (left != right) {
        if (spin_held < 40) spin_held++;
        int step = 4 + spin_held / 3;
        if (step > 10) step = 10;
        spin = left ? -step : step;
    } else {
        spin_held = 0;
        if (lx > 300 || lx < -300) spin = lx * 12 / GP_STICK_MAX;
    }
    st_in.dial = (int8_t)spin;

    static bool coin_held;
    static int coins_owed, coin_down, coin_up, primed;
    bool coin_switch;
    if (primed < 4) {                       // the first press and release, before anything (see above)
        coin_switch = primed < 2;
        primed++;
        coin_held = coin;
    } else {
        if (coin && !coin_held && coins_owed < 2) coins_owed++;
        coin_held = coin;
        if (!coin_down && !coin_up && coins_owed) { coins_owed--; coin_down = 6; }
        coin_switch = coin_down > 0;
        if (coin_down) { if (!--coin_down) coin_up = 3; }
        else if (coin_up) coin_up--;
    }
    uint8_t fc = 0;
    if (start1) fc |= 0x01;
    if (start2) fc |= 0x02;
    if (photon) fc |= 0x04;
    if (phasers) fc |= 0x08;
    if (impulse) fc |= 0x10;
    if (warp) fc |= 0x20;
    st_in.fc = fc;
    st_in.d7d6 = (uint8_t)(coin_switch ? st_in.d7d6 & ~0x01 : st_in.d7d6 | 0x01);
}

// ---- the game ------------------------------------------------------------------------
static uint32_t published, frames_at_start, us_max, pics_at_note;

// one list: the controls, the board for the list's time, the list made,
// then handed over as soon as core 1 takes it
static void game_list(void) {
    uint32_t t0 = time_us_32();
    controls();
    list_len = strun_list(&run, list);
    uint32_t us = time_us_32() - t0;
    if (us > us_max) us_max = us;
    while (!cvf_publish(list, list_len)) tasks();
    published++;
    tasks();
}

// the menu's bottom line, should RESET come: pictures a second, lists the
// console ran again for want of a new one (since the start), the slowest
// list this second, the bus's odd selects
static void update_note(void) {
    static uint32_t t_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    int32_t again = (int32_t)(cvf_frames() - frames_at_start) - (int32_t)(published - 1);
    if (again < 0) again = 0;
    unsigned per_s = MIN(run.pictures - pics_at_note, 99u), ms = MIN(us_max / 1000, 99u), tenths = us_max / 100 % 10;
    uint32_t unsel, early, unhdr;
    cvf_bus_counts(&unsel, &early, &unhdr);
    char n[48];
    snprintf(n, sizeof n, TAG " %u/s %ldL %u.%ums s%u h%u", per_s, (long)MIN(again, 9999), ms, tenths,
             (unsigned)MIN(unsel, 999u), (unsigned)MIN(unhdr, 999u));
    cvf_note(n);
    t_last = t;
    pics_at_note = run.pictures;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    cvd_stage(1);
    cvusb_start();                          // after the kernel's start: a host, for what is plugged into the hub
    stcv_init(&enc);
    scratch = stcv_scratch(&scratch_n);
    message("LOOKING FOR THE ROMS", "IN " STCARD_HOME " ON THE CARD", NULL, NULL);
    serve_for(OPEN_US);

    if (!cvsd_card_in() || !cvsd_init()) fail(cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT", NULL, NULL, GAME ": no card");
    // the card's state and its reads in the scratch (no message is drawn
    // until they are done with: fail() draws over them)
    fs_t *fs = (fs_t *)(void *)scratch;
    size_t fs_room = (sizeof(fs_t) + 7) & ~(size_t)7;
    if (fs_mount(fs, cvsd_read) != FS_OK) fail("NO FAT OR EXFAT ON THE CARD", NULL, NULL, GAME ": card didn't mount");
    cvd_stage(2);
    watchdog_enable(WATCHDOG_MS, true);
    uint32_t found = stcard_load_roms(fs, scratch + fs_room, (uint32_t)(scratch_n - fs_room), card_tick);
    cvd_stage(3);
    if (found != STROMS_ALL) {
        int missing = 0;
        const char *first = "";
        for (int i = 0; i < ST_ROM_FILES; i++)
            if (!(found & 1u << i) && !missing++) first = st_rom_files[i].name;
        char l2[40], note[48];
        if (!found) snprintf(l2, sizeof l2, "NONE OF ITS %d FILES IS THERE", ST_ROM_FILES);
        else snprintf(l2, sizeof l2, "%d OF %d MISSING: %.14s", missing, ST_ROM_FILES, first);
        for (char *c = l2; *c; c++)
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        snprintf(note, sizeof note, GAME ": %d ROMs missing", missing);   // (32 characters at most)
        fail("PUT MAME'S STARTREK ROMS, OR", "STARTREK.ZIP, IN " STCARD_HOME, l2, note);
    }
    stroms_roms(&roms);
    st_in.d3d2 = ST_D3D2;                   // MAME's defaults
    st_in.d1d0 = ST_D1D0;
    st_init(&roms);
    strun_start(&run, &enc);
    cvd_stage(4);
    cvf_note(GAME);

    while (list_pending) tasks(), publish();          // the last message's list out first
    frames_at_start = cvf_frames();
    for (;;) {
        game_list();
        update_note();
    }
}
