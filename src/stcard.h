// stcard.h -- the ROMs on the card, through fsro: loose or zipped in
// /STARTREK (MAME's startrek.zip, as it is), or /STARTREK.ZIP. Portable: the
// plugin and the Mac test (test/st/card.sh, on disk images) share it.
// (Time Pilot's tpcard.c, from Donkey Kong's.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "cvsd.h"                       // (fs_t: the SDK's sdplayer/fsro.h)
#include "stroms.h"

#define STCARD_HOME "/STARTREK"
#define STCARD_ZIP  "/STARTREK.ZIP"

// Every file in STCARD_HOME, then STCARD_ZIP, until all are found: each
// ROM to its place (stroms.h). A zip is never read whole: its directory,
// then each wanted entry's packed bytes into buf and unpacked after them.
// buf: max bytes (the directory, and a packed ROM with the ROM after it,
// must fit); tick: called between sectors (the plugin serves the
// console), or NULL. Returns stroms' found bits.
uint32_t stcard_load_roms(fs_t *fs, uint8_t *buf, uint32_t max, void (*tick)(void));
