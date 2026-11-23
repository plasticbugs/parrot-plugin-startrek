// snd_host.c -- the board and stsnd.c's sound on the Mac, as a WAV, for
// test/st/soundcmp.sh: the same game as MAME's -wavwrite.
//
//   snd_host ROMDIR SCRIPT FRAMES OUT.wav [SPEECH.wav USB.wav]
//
// The sound in MAME's units (1.0 full scale) at the lists' rate (5,962 a
// second), 16 bits; the script's lines as st_host's ("frame name value").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st.h"
#include "stcv.h"
#include "stsnd.h"

static uint8_t maincpu[0xc000], speech_cpu[0x800], speech_data[0x4000], sine[0x400];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "snd_host: can't read %s\n", path); exit(1); }
    fclose(f);
}

static struct { int frame; char name[16]; int v; } ev[65536];
static int nev;

static void apply(const char *name, int v) {
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

static void on_vblank(void) {
    for (int i = 0; i < nev; i++)
        if (ev[i].frame == (int)st_frames) apply(ev[i].name, ev[i].v);
}

static FILE *wav_open(const char *name, uint32_t rate) {
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "snd_host: can't write %s\n", name); exit(1); }
    uint8_t h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0";
    memcpy(h + 24, &rate, 4);
    uint32_t br = rate * 2;
    memcpy(h + 28, &br, 4);
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4);
    fwrite(h, 1, 44, f);
    return f;
}
static void wav_close(FILE *f) {
    long n = ftell(f) - 44;
    uint32_t a = (uint32_t)n + 36, b = (uint32_t)n;
    fseek(f, 4, SEEK_SET); fwrite(&a, 4, 1, f);
    fseek(f, 40, SEEK_SET); fwrite(&b, 4, 1, f);
    fclose(f);
}
static void put(FILE *f, float v) {
    if (!f) return;
    float x = v * 32767.0f;
    int16_t s = (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
    fwrite(&s, 2, 1, f);
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: snd_host ROMDIR SCRIPT FRAMES OUT.wav [SPEECH.wav USB.wav]\n"); return 2; }
    const char *dir = argv[1];
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
    if (argv[2][0]) {
        FILE *f = fopen(argv[2], "r");
        char line[256];
        while (f && fgets(line, sizeof line, f) && nev < 65536)
            if (sscanf(line, "%d %15s %d", &ev[nev].frame, ev[nev].name, &ev[nev].v) == 3) nev++;
        if (f) fclose(f);
    }
    int frames = atoi(argv[3]);
    uint32_t rate = (uint32_t)((uint64_t)STCV_PCM_N * STCV_CONSOLE_HZ / STCV_LIST_T);
    FILE *out = wav_open(argv[4], rate), *fsp = argc > 6 ? wav_open(argv[5], rate) : NULL,
         *fusb = argc > 6 ? wav_open(argv[6], rate) : NULL;
    static const st_roms_t roms = {maincpu, speech_cpu, speech_data, sine};
    st_on_vblank = on_vblank;
    st_init(&roms);
    stsnd_init();
    uint64_t lists = 0;
    uint64_t n = 0;
    while (st_frames < (uint32_t)frames) {
        lists++;
        st_run_to(((uint64_t)lists * STCV_LIST_CYCLES_Q16 >> 16) + 2600);
        float s[STCV_PCM_N], a[STCV_PCM_N], b[STCV_PCM_N];
        stsnd_mame(s, STCV_PCM_N, a, b);
        for (int k = 0; k < STCV_PCM_N; k++) {
            put(out, s[k]);
            put(fsp, a[k]);
            put(fusb, b[k]);
            n++;
        }
    }
    wav_close(out);
    if (fsp) wav_close(fsp);
    if (fusb) wav_close(fusb);
    printf("snd_host: %d frames, %llu samples at %u Hz\n", frames, (unsigned long long)n, rate);
#ifdef STSND_STATS
    extern uint32_t stsnd_wq_max;
    printf("snd_host: at most %u of the board's sound writes waiting\n", (unsigned)stsnd_wq_max);
#endif
    return 0;
}
