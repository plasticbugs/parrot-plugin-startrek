// stroms.h -- where the ROMs go: MAME's "startrek" set, each file known by
// its size and CRC-32, never by its name. Portable: the plugin and the Mac
// tests share it. (Time Pilot's tproms.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "st.h"

typedef struct {
    const char *name;                   // MAME's name, for messages
    uint32_t size, crc;
    uint32_t at;                        // where its bytes go in the store
} st_rom_file_t;

// the CPU board's ROM and the PROM board's 23, the speech board's program
// and its two data ROMs, and the vector board's sine PROM (the two
// addressing PROMs MAME lists aren't used)
#define ST_ROM_FILES 28
#define STROMS_ALL   ((1u << ST_ROM_FILES) - 1)
extern const st_rom_file_t st_rom_files[ST_ROM_FILES];

typedef struct {
    uint8_t maincpu[0xc000], speech_cpu[0x800], speech_data[0x2000], sine[0x400];
} st_rom_store_t;
extern st_rom_store_t st_rom_store;

uint32_t st_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
// which of the files still to find (not in found) has this size and CRC-32 (-1: none)
int stroms_wanted(uint32_t size, uint32_t crc, uint32_t found);
bool stroms_size_ok(uint32_t size);     // one of the files' sizes
// a file's bytes (size of them, its CRC-32 right) to its place
void stroms_put(int i, const uint8_t *data);
// the board's ROMs, from the store
void stroms_roms(st_roms_t *r);
