// stroms.c -- see stroms.h. The sizes and CRC-32s are MAME 0.288's
// (segag80v.cpp's startrek).
#include "stroms.h"

#include <stddef.h>
#include <string.h>

st_rom_store_t st_rom_store;

#define M(o) (uint32_t)(offsetof(st_rom_store_t, maincpu) + (o))
#define S(o) (uint32_t)(offsetof(st_rom_store_t, speech_data) + (o))
const st_rom_file_t st_rom_files[ST_ROM_FILES] = {
    {"1873.cpu-u25", 0x800, 0xbe46f5d9u, M(0x0000)},
    {"1848.prom-u1", 0x800, 0x65e3baf3u, M(0x0800)},
    {"1849.prom-u2", 0x800, 0x8169fd3du, M(0x1000)},
    {"1850.prom-u3", 0x800, 0x78fd68dcu, M(0x1800)},
    {"1851.prom-u4", 0x800, 0x3f55ab86u, M(0x2000)},
    {"1852.prom-u5", 0x800, 0x2542ecfbu, M(0x2800)},
    {"1853.prom-u6", 0x800, 0x75c2526au, M(0x3000)},
    {"1854.prom-u7", 0x800, 0x096d75d0u, M(0x3800)},
    {"1855.prom-u8", 0x800, 0xbc7b9a12u, M(0x4000)},
    {"1856.prom-u9", 0x800, 0xed9fe2fbu, M(0x4800)},
    {"1857.prom-u10", 0x800, 0x28699d45u, M(0x5000)},
    {"1858.prom-u11", 0x800, 0x3a7593cbu, M(0x5800)},
    {"1859.prom-u12", 0x800, 0x5b11886bu, M(0x6000)},
    {"1860.prom-u13", 0x800, 0x62eb96e6u, M(0x6800)},
    {"1861.prom-u14", 0x800, 0x99852d1du, M(0x7000)},
    {"1862.prom-u15", 0x800, 0x76ce27b2u, M(0x7800)},
    {"1863.prom-u16", 0x800, 0xdd92d187u, M(0x8000)},
    {"1864.prom-u17", 0x800, 0xe37d3a1eu, M(0x8800)},
    {"1865.prom-u18", 0x800, 0xb2ec8125u, M(0x9000)},
    {"1866.prom-u19", 0x800, 0x6f188354u, M(0x9800)},
    {"1867.prom-u20", 0x800, 0xb0a3eae8u, M(0xa000)},
    {"1868.prom-u21", 0x800, 0x8b4e2e07u, M(0xa800)},
    {"1869.prom-u22", 0x800, 0xe5663070u, M(0xb000)},
    {"1870.prom-u23", 0x800, 0x4340616du, M(0xb800)},
    {"1607.speech-u7", 0x800, 0xb779884bu, (uint32_t)offsetof(st_rom_store_t, speech_cpu)},
    {"1871.speech-u6", 0x1000, 0x03713920u, S(0x0000)},
    {"1872.speech-u5", 0x1000, 0xebb5c3a9u, S(0x1000)},
    {"s-c.xyt-u39", 0x400, 0x56484d19u, (uint32_t)offsetof(st_rom_store_t, sine)},
};
#undef M
#undef S

uint32_t st_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

int stroms_wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < ST_ROM_FILES; i++)
        if (st_rom_files[i].size == size && st_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

bool stroms_size_ok(uint32_t size) {
    for (int i = 0; i < ST_ROM_FILES; i++)
        if (st_rom_files[i].size == size) return true;
    return false;
}

void stroms_put(int i, const uint8_t *data) {
    const st_rom_file_t *f = &st_rom_files[i];
    memmove((uint8_t *)&st_rom_store + f->at, data, f->size);
}

void stroms_roms(st_roms_t *r) {
    r->maincpu = st_rom_store.maincpu;
    r->speech_cpu = st_rom_store.speech_cpu;
    r->speech_data = st_rom_store.speech_data;
    r->sine = st_rom_store.sine;
}
