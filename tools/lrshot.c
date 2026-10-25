// Minimal headless libretro frontend: run a core for N frames, dump the last
// video frame as a PPM, and optionally dump the core's system RAM.
// No X, no GL, no window (screen-safe).
//
// build: gcc -O2 -o lrshot lrshot.c -ldl
// usage: ./lrshot <core.so> <rom> [frames] [out.ppm]
//   LRSHOT_DUMP=<path>   also write RETRO_MEMORY_SYSTEM_RAM to <path>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libretro.h"

// ---------------------------------------------------------------------------
// Minimal stdio-backed VFS (desmume needs it; otherwise it crashes on a NULL
// file-op pointer during retro_run).
// ---------------------------------------------------------------------------
struct retro_vfs_file_handle { FILE* f; char path[4096]; };
struct retro_vfs_dir_handle { DIR* d; struct dirent* e; char name[1024]; bool is_dir; };

static const char* vfs_get_path(struct retro_vfs_file_handle* s) { return s ? s->path : NULL; }

static struct retro_vfs_file_handle* vfs_open(const char* path, unsigned mode, unsigned hints)
{
    (void)hints;
    const char* m = "rb";
    if ((mode & RETRO_VFS_FILE_ACCESS_READ_WRITE) == RETRO_VFS_FILE_ACCESS_READ_WRITE) m = "r+b";
    else if (mode & RETRO_VFS_FILE_ACCESS_WRITE) m = "wb";
    FILE* f = fopen(path, m);
    if (!f) return NULL;
    struct retro_vfs_file_handle* h = (struct retro_vfs_file_handle*)calloc(1, sizeof(*h));
    h->f = f;
    snprintf(h->path, sizeof(h->path), "%s", path);
    return h;
}
static int vfs_close(struct retro_vfs_file_handle* s) { if (!s) return -1; fclose(s->f); free(s); return 0; }
static int64_t vfs_size(struct retro_vfs_file_handle* s)
{
    if (!s) return -1;
    long p = ftell(s->f); fseek(s->f, 0, SEEK_END); long e = ftell(s->f); fseek(s->f, p, SEEK_SET);
    return e;
}
static int64_t vfs_tell(struct retro_vfs_file_handle* s) { return s ? ftell(s->f) : -1; }
static int64_t vfs_seek(struct retro_vfs_file_handle* s, int64_t off, int whence)
{
    int w = whence == RETRO_VFS_SEEK_POSITION_START ? SEEK_SET
          : whence == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR : SEEK_END;
    return s ? fseek(s->f, (long)off, w) : -1;
}
static int64_t vfs_read(struct retro_vfs_file_handle* s, void* buf, uint64_t len)
{
    return s ? (int64_t)fread(buf, 1, (size_t)len, s->f) : -1;
}
static int64_t vfs_write(struct retro_vfs_file_handle* s, const void* buf, uint64_t len)
{
    return s ? (int64_t)fwrite(buf, 1, (size_t)len, s->f) : -1;
}
static int vfs_flush(struct retro_vfs_file_handle* s) { return s ? fflush(s->f) : -1; }
static int vfs_remove(const char* path) { return remove(path); }
static int vfs_rename(const char* a, const char* b) { return rename(a, b); }
static int64_t vfs_truncate(struct retro_vfs_file_handle* s, int64_t length)
{
    if (!s) return -1;
    fflush(s->f);
    return ftruncate(fileno(s->f), (off_t)length) == 0 ? length : -1;
}
static int vfs_stat(const char* path, int32_t* size)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (size) *size = (int32_t)st.st_size;
    return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(st.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
}
static int vfs_mkdir(const char* dir) { return mkdir(dir, 0755); }
static struct retro_vfs_dir_handle* vfs_opendir(const char* dir, bool include_hidden)
{
    (void)include_hidden;
    DIR* d = opendir(dir);
    if (!d) return NULL;
    struct retro_vfs_dir_handle* h = (struct retro_vfs_dir_handle*)calloc(1, sizeof(*h));
    h->d = d;
    return h;
}
static bool vfs_readdir(struct retro_vfs_dir_handle* h)
{
    if (!h) return false;
    h->e = readdir(h->d);
    if (!h->e) return false;
    snprintf(h->name, sizeof(h->name), "%s", h->e->d_name);
    struct stat st;
    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", ".", h->name);
    h->is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
    return true;
}
static const char* vfs_dirent_get_name(struct retro_vfs_dir_handle* h) { return h ? h->name : NULL; }
static bool vfs_dirent_is_dir(struct retro_vfs_dir_handle* h) { return h ? h->is_dir : false; }
static int vfs_closedir(struct retro_vfs_dir_handle* h) { if (!h) return -1; closedir(h->d); free(h); return 0; }

static struct retro_vfs_interface g_vfs = {
    vfs_get_path, vfs_open, vfs_close, vfs_size, vfs_tell, vfs_seek, vfs_read, vfs_write,
    vfs_flush, vfs_remove, vfs_rename, vfs_truncate, vfs_stat, vfs_mkdir, vfs_opendir,
    vfs_readdir, vfs_dirent_get_name, vfs_dirent_is_dir, vfs_closedir,
};

// ---------------------------------------------------------------------------
static enum retro_pixel_format g_pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static const char* g_out = "shot.ppm";
static const char* g_sys = "/tmp/lrshot/sys";
static const char* g_save = "/tmp/lrshot/save";

// --- core option storage (serve the core its own defaults) -----------------
#define MAX_OPTS 160
static char g_opt_keys[MAX_OPTS][64];
static char g_opt_vals[MAX_OPTS][128];
static int g_opt_count;

static void opt_store(const char* key, const char* val)
{
    if (!key || !key[0]) return;
    for (int i = 0; i < g_opt_count; i++)
        if (strcmp(g_opt_keys[i], key) == 0)
        {
            snprintf(g_opt_vals[i], sizeof(g_opt_vals[i]), "%s", val ? val : "");
            return;
        }
    if (g_opt_count >= MAX_OPTS) return;
    snprintf(g_opt_keys[g_opt_count], sizeof(g_opt_keys[0]), "%s", key);
    snprintf(g_opt_vals[g_opt_count], sizeof(g_opt_vals[0]), "%s", val ? val : "");
    g_opt_count++;
}
// Overrides from LRSHOT_SET_OPTIONS="key=value;key2=value2" take priority over
// whatever defaults the core registers, so a core option can be forced.
#define MAX_OVR 32
static char g_ovr_keys[MAX_OVR][64];
static char g_ovr_vals[MAX_OVR][128];
static int g_ovr_count;

static void opt_override(const char* key, const char* val)
{
    if (!key || !key[0] || g_ovr_count >= MAX_OVR) return;
    snprintf(g_ovr_keys[g_ovr_count], sizeof(g_ovr_keys[0]), "%s", key);
    snprintf(g_ovr_vals[g_ovr_count], sizeof(g_ovr_vals[0]), "%s", val ? val : "");
    g_ovr_count++;
}
static void opt_overrides_from_env(void)
{
    const char* env = getenv("LRSHOT_SET_OPTIONS");
    if (!env) return;
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", env);
    char* save = NULL;
    for (char* tok = strtok_r(buf, ";", &save); tok; tok = strtok_r(NULL, ";", &save))
    {
        char* eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        opt_override(tok, eq + 1);
    }
}
static const char* opt_get(const char* key)
{
    if (!key) return NULL;
    for (int i = 0; i < g_ovr_count; i++)
        if (strcmp(g_ovr_keys[i], key) == 0)
        {
            fprintf(stderr, "opt override %s=%s\n", key, g_ovr_vals[i]);
            return g_ovr_vals[i];
        }
    for (int i = 0; i < g_opt_count; i++)
        if (strcmp(g_opt_keys[i], key) == 0) return g_opt_vals[i];
    return NULL;
}
static void opt_from_variable(const char* key, const char* desc)
{
    // "Description; default|alt1|alt2" -> "default"
    const char* semi = desc ? strchr(desc, ';') : NULL;
    if (!semi) { opt_store(key, ""); return; }
    semi++; while (*semi == ' ') semi++;
    const char* bar = strchr(semi, '|');
    size_t n = bar ? (size_t)(bar - semi) : strlen(semi);
    if (n >= sizeof(g_opt_vals[0])) n = sizeof(g_opt_vals[0]) - 1;
    char buf[128];
    memcpy(buf, semi, n); buf[n] = 0;
    opt_store(key, buf);
}

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
                r = ((v >> 11) & 0x1F) * 255 / 31; g = ((v >> 5) & 0x3F) * 255 / 63; b = (v & 0x1F) * 255 / 31;
            }
            else
            {
                uint16_t v = ((const uint16_t*)row)[x];
                r = ((v >> 10) & 0x1F) * 255 / 31; g = ((v >> 5) & 0x1F) * 255 / 31; b = (v & 0x1F) * 255 / 31;
            }
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    }
    fclose(f);
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || w == 0 || h == 0) return;
    write_ppm(g_out, data, w, h, pitch);
}
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t* d, size_t frames) { (void)d; return frames; }
static void input_poll_cb(void) { }
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }
static void log_printf(enum retro_log_level level, const char* fmt, ...) { (void)level; (void)fmt; }

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
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
        {
            struct retro_vfs_interface_info* info = (struct retro_vfs_interface_info*)data;
            info->iface = &g_vfs;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        {
            const struct retro_variable* v = (const struct retro_variable*)data;
            for (; v && v->key; v++) opt_from_variable(v->key, v->value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        {
            const struct retro_core_option_definition* d = (const struct retro_core_option_definition*)data;
            for (; d && d->key; d++) opt_store(d->key, d->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        {
            const struct retro_core_options_intl* intl = (const struct retro_core_options_intl*)data;
            const struct retro_core_option_definition* d = intl ? intl->us : NULL;
            for (; d && d->key; d++) opt_store(d->key, d->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        {
            const struct retro_core_options_v2* v2 = (const struct retro_core_options_v2*)data;
            const struct retro_core_option_v2_definition* d = v2 ? v2->definitions : NULL;
            for (; d && d->key; d++) opt_store(d->key, d->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable* v = (struct retro_variable*)data;
            v->value = opt_get(v->key);
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
    opt_overrides_from_env();

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
    void* (*get_mem_data)(unsigned) = (void* (*)(unsigned))dlsym(handle, "retro_get_memory_data");
    size_t (*get_mem_size)(unsigned) = (size_t (*)(unsigned))dlsym(handle, "retro_get_memory_size");
#undef LOAD

    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    retro_init();

    // Some cores (melonDS) declare need_fullpath=false and expect the frontend
    // to supply the ROM in memory; others (DeSmuME) use the path only. Provide
    // both so the same harness works with every DS core.
    FILE* rom_file = fopen(rom_path, "rb");
    if (!rom_file) { perror("fopen(rom)"); return 1; }
    fseek(rom_file, 0, SEEK_END);
    long rom_size = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);
    void* rom_data = malloc((size_t)rom_size);
    if (!rom_data || fread(rom_data, 1, (size_t)rom_size, rom_file) != (size_t)rom_size)
    {
        fprintf(stderr, "could not read rom %s\n", rom_path);
        fclose(rom_file);
        return 1;
    }
    fclose(rom_file);

    struct retro_game_info gi;
    memset(&gi, 0, sizeof(gi));
    gi.path = rom_path;
    gi.data = rom_data;
    gi.size = (size_t)rom_size;
    if (!retro_load_game(&gi))
    {
        fprintf(stderr, "retro_load_game failed\n");
        free(rom_data);
        retro_deinit();
        return 1;
    }
    free(rom_data);

    for (int i = 0; i < frames; i++)
        retro_run();

    void* sysram = NULL;
    size_t sysram_size = 0;
    if (get_mem_data && get_mem_size)
    {
        sysram = get_mem_data(RETRO_MEMORY_SYSTEM_RAM);
        sysram_size = get_mem_size(RETRO_MEMORY_SYSTEM_RAM);
    }

    const char* dump = getenv("LRSHOT_DUMP");
    if (dump && sysram && sysram_size)
    {
        FILE* f = fopen(dump, "wb");
        if (f) { fwrite(sysram, 1, sysram_size, f); fclose(f); fprintf(stderr, "dumped %zu bytes system RAM\n", sysram_size); }
    }
    else if (dump) fprintf(stderr, "system RAM not exposed by core\n");

    // GBARunner headless report (see code/core/arm9/source/Emulator/TestReport.h).
    // LRSHOT_REPORT is the *DS address* of gTestReport (read from the ELF by the
    // test script); the struct lives in system RAM at 0x02000000..0x023FFFFF.
    const char* rep = getenv("LRSHOT_REPORT");
    if (rep && sysram && sysram_size)
    {
        unsigned long addr = strtoul(rep, NULL, 0);
        if (addr >= 0x02000000ul && addr + 24 <= 0x02000000ul + sysram_size)
        {
            const uint32_t* r = (const uint32_t*)((const uint8_t*)sysram + (addr - 0x02000000ul));
            printf("REPORT magic=%08x stage=%u result=%u code=%08x frame=%08x debug=%08x\n",
                   r[0], r[1], r[2], r[3], r[4], r[5]);
            fflush(stdout);
        }
        else fprintf(stderr, "report address %s is outside system RAM\n", rep);
    }

    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    return 0;
}
