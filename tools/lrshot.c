// Minimal headless libretro frontend: run a core for N frames and dump the
// last video frame as a PPM (no X, no GL, no window). Screen-safe.
//
// build: gcc -O2 -o lrshot lrshot.c -ldl
// usage: ./lrshot <core.so> <rom> [frames] [out.ppm] [--input-on frame:btn]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "libretro.h"

static enum retro_pixel_format g_pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static const char* g_out = "shot.ppm";
static const char* g_sys = "/tmp/lrshot/sys";
static const char* g_save = "/tmp/lrshot/save";

static void write_ppm(const char* path, const void* data, unsigned w, unsigned h, size_t pitch)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror("fopen"); return; }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned y = 0; y < h; y++)
    {
        const uint8_t* row = (const uint8_t*)data + y * pitch;
        for (unsigned x = 0; x < w; x++)
        {
            uint8_t r, g, b;
            if (g_pixfmt == RETRO_PIXEL_FORMAT_XRGB8888)
            {
                uint32_t v = ((const uint32_t*)row)[x];
                r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
            }
            else if (g_pixfmt == RETRO_PIXEL_FORMAT_RGB565)
            {
                uint16_t v = ((const uint16_t*)row)[x];
                r = ((v >> 11) & 0x1F) * 255 / 31;
                g = ((v >> 5) & 0x3F) * 255 / 63;
                b = (v & 0x1F) * 255 / 31;
            }
            else // 0RGB1555
            {
                uint16_t v = ((const uint16_t*)row)[x];
                r = ((v >> 10) & 0x1F) * 255 / 31;
                g = ((v >> 5) & 0x1F) * 255 / 31;
                b = (v & 0x1F) * 255 / 31;
            }
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    }
    fclose(f);
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0) return; // dupe frame
    write_ppm(g_out, data, w, h, pitch);   // last frame wins
}

static void log_printf(enum retro_log_level level, const char* fmt, ...)
{
    (void)level; (void)fmt;
}

static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t* d, size_t frames) { (void)d; return frames; }
static void input_poll_cb(void) { }
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)device; (void)index; (void)id; return 0;
}

static bool env_cb(unsigned cmd, void* data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char**)data = (cmd == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY) ? g_sys : g_save;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            g_pixfmt = *(const enum retro_pixel_format*)data;
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable* v = (struct retro_variable*)data;
            v->value = NULL; // core default
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool*)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool*)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            struct retro_log_callback* cb = (struct retro_log_callback*)data;
            cb->log = log_printf;
            return true;
        }
        default:
            return false;
    }
}

typedef void (*t_set_environment)(retro_environment_t);
typedef void (*t_set_video_refresh)(retro_video_refresh_t);
typedef void (*t_set_audio_sample)(retro_audio_sample_t);
typedef void (*t_set_audio_sample_batch)(retro_audio_sample_batch_t);
typedef void (*t_set_input_poll)(retro_input_poll_t);
typedef void (*t_set_input_state)(retro_input_state_t);
typedef void (*t_void)(void);
typedef bool (*t_load_game)(const struct retro_game_info*);

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <core.so> <rom> [frames] [out.ppm]\n", argv[0]);
        return 2;
    }
    const char* core_path = argv[1];
    const char* rom_path = argv[2];
    int frames = argc > 3 ? atoi(argv[3]) : 300;
    if (argc > 4) g_out = argv[4];

    mkdir("/tmp/lrshot", 0755);
    mkdir(g_sys, 0755);
    mkdir(g_save, 0755);

    void* handle = dlopen(core_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define LOAD(ty, name) ty name = (ty)dlsym(handle, #name); \
    if (!name) { fprintf(stderr, "missing symbol %s\n", #name); return 1; }
    LOAD(t_set_environment, retro_set_environment);
    LOAD(t_set_video_refresh, retro_set_video_refresh);
    LOAD(t_set_audio_sample, retro_set_audio_sample);
    LOAD(t_set_audio_sample_batch, retro_set_audio_sample_batch);
    LOAD(t_set_input_poll, retro_set_input_poll);
    LOAD(t_set_input_state, retro_set_input_state);
    LOAD(t_void, retro_init);
    LOAD(t_void, retro_deinit);
    LOAD(t_load_game, retro_load_game);
    LOAD(t_void, retro_unload_game);
    LOAD(t_void, retro_run);
#undef LOAD

    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();

    struct retro_game_info gi;
    memset(&gi, 0, sizeof(gi));
    gi.path = rom_path;
    if (!retro_load_game(&gi))
    {
        fprintf(stderr, "retro_load_game failed\n");
        retro_deinit();
        return 1;
    }

    for (int i = 0; i < frames; i++)
        retro_run();

    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    return 0;
}
