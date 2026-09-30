/*
 * X12 Adaptive Performance Engine — Core Implementation
 * ======================================================
 * x12_engine.c
 *
 * Four-pillar formula:
 *
 *   SPEED      = O3 + LTO build flags + hot-path SIMD dispatch
 *   SMOOTHNESS = frame-pacing loop + render backend selection
 *   ADAPTIVE   = runtime hardware probe → mode selection matrix
 *   AUTOMATIC  = background monitor thread → continuous re-evaluation
 *
 * Decision matrix (automatic mode selection):
 *
 *   thermal=HOT  OR  ram<512MB           → POWER_SAVE
 *   battery<20%  AND not charging        → POWER_SAVE
 *   battery<50%  AND thermal=WARM        → BALANCED
 *   plugged      OR  thermal=COOL        → PERFORMANCE
 *   perf_request AND plugged AND cool    → TURBO
 */

#define _GNU_SOURCE
#include "x12_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <strings.h>
#if defined(__linux__) || defined(__ANDROID__)
#include <sys/sysinfo.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#include <sys/stat.h>

#ifdef __aarch64__
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
#endif

/* ── Internal constants ───────────────────────────────────────────────── */
#define X12_MONITOR_INTERVAL_MS   2000   /* re-evaluate every 2 s        */
#define X12_FRAME_HISTORY         16     /* frame time ring buffer size  */
#define X12_THERMAL_THROTTLE_TEMP 80     /* °C → drop to POWER_SAVE      */
#define X12_LOW_RAM_KB            (2ULL * 1024 * 1024)  /* 2 GB          */

/* ── Sysfs paths (Android/Termux) ────────────────────────────────────── */
#define SYSFS_CPU_ONLINE    "/sys/devices/system/cpu/online"
#define SYSFS_CPU_FREQ_MAX  "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"
#define SYSFS_THERMAL_ZONE  "/sys/class/thermal/thermal_zone0/temp"
#define SYSFS_BATTERY_CAP   "/sys/class/power_supply/battery/capacity"
#define SYSFS_BATTERY_STAT  "/sys/class/power_supply/battery/status"
#define PROC_CPUINFO        "/proc/cpuinfo"
#define PROC_MEMINFO        "/proc/meminfo"

/* ── Internal engine struct ───────────────────────────────────────────── */
struct X12Engine {
    X12Config           cfg;
    X12HardwareProfile  hw;
    X12PerfMode         mode;
    X12RenderHint       render_backend;

    /* Frame pacing */
    struct timespec     frame_start;
    uint64_t            frame_times_us[X12_FRAME_HISTORY];
    int                 frame_idx;
    uint64_t            frame_budget_us;   /* target frame time */

    /* Background monitor */
    pthread_t           monitor_thread;
    volatile bool       monitor_running;
    pthread_mutex_t     lock;
};

/* ── Utility helpers ──────────────────────────────────────────────────── */

static uint64_t x12_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

static int x12_read_sysfs_int(const char *path, int default_val) {
    FILE *f = fopen(path, "r");
    if (!f) return default_val;
    int val = default_val;
    fscanf(f, "%d", &val);
    fclose(f);
    return val;
}

static bool x12_read_sysfs_str(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(buf, (int)len, f)) { fclose(f); return false; }
    /* strip newline */
    buf[strcspn(buf, "\n")] = '\0';
    fclose(f);
    return true;
}

/* ── Hardware detection ───────────────────────────────────────────────── */

static void x12_detect_cpu(X12HardwareProfile *hw) {
    /* Architecture */
#if defined(__aarch64__)
    strncpy(hw->cpu_arch, "aarch64", sizeof(hw->cpu_arch));
#elif defined(__arm__)
    strncpy(hw->cpu_arch, "arm", sizeof(hw->cpu_arch));
#elif defined(__x86_64__)
    strncpy(hw->cpu_arch, "x86_64", sizeof(hw->cpu_arch));
#else
    strncpy(hw->cpu_arch, "unknown", sizeof(hw->cpu_arch));
#endif

    /* Core count */
#if defined(__linux__) || defined(__ANDROID__)
    hw->cpu_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#else
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    hw->cpu_cores = (int)si.dwNumberOfProcessors;
#endif
    if (hw->cpu_cores <= 0) hw->cpu_cores = 2;

    /* Detect big.LITTLE split: read /proc/cpuinfo for different max freqs */
    {
        long max_freq = 0, min_max_freq = LONG_MAX;
        char path[64];
        int perf = 0, eff = 0;
        for (int i = 0; i < hw->cpu_cores; i++) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
            long f = x12_read_sysfs_int(path, 0);
            if (f > max_freq) max_freq = f;
            if (f > 0 && f < min_max_freq) min_max_freq = f;
        }
        hw->cpu_freq_max_khz = (uint64_t)max_freq;
        /* cores running above 80% of max_freq are "perf" cores */
        for (int i = 0; i < hw->cpu_cores; i++) {
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
            long f = x12_read_sysfs_int(path, (int)max_freq);
            if (f >= (long)(max_freq * 0.8)) perf++;
            else eff++;
        }
        hw->cpu_perf_cores = perf > 0 ? perf : hw->cpu_cores;
        hw->cpu_eff_cores  = eff;
    }

    /* CPU model from /proc/cpuinfo */
    {
        FILE *f = fopen(PROC_CPUINFO, "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "Hardware", 8) == 0 ||
                    strncmp(line, "model name", 10) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        colon++;
                        while (*colon == ' ') colon++;
                        strncpy(hw->cpu_model, colon, sizeof(hw->cpu_model)-1);
                        hw->cpu_model[strcspn(hw->cpu_model, "\n")] = '\0';
                        break;
                    }
                }
            }
            fclose(f);
        }
    }

    /* SIMD capabilities */
#ifdef __aarch64__
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    hw->has_neon    = !!(hwcap  & HWCAP_ASIMD);
    hw->has_sve     = !!(hwcap  & HWCAP_SVE);
    hw->has_dotprod = !!(hwcap  & HWCAP_ASIMDDP);
    (void)hwcap2;
#elif defined(__arm__)
    unsigned long hwcap = getauxval(AT_HWCAP);
    hw->has_neon = !!(hwcap & HWCAP_NEON);
#else
    /* x86: check for SSE/AVX via CPUID — simplified */
    hw->has_neon = false;
#endif
}

static void x12_detect_memory(X12HardwareProfile *hw) {
#if defined(__linux__) || defined(__ANDROID__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        hw->ram_total_kb = (uint64_t)si.totalram * si.mem_unit / 1024ULL;
        hw->ram_avail_kb = (uint64_t)si.freeram  * si.mem_unit / 1024ULL
                         + (uint64_t)si.bufferram * si.mem_unit / 1024ULL;
    } else {
        /* fallback: read /proc/meminfo */
        FILE *f = fopen(PROC_MEMINFO, "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "MemTotal:", 9) == 0)
                    sscanf(line + 9, " %llu", &hw->ram_total_kb);
                else if (strncmp(line, "MemAvailable:", 13) == 0)
                    sscanf(line + 13, " %llu", &hw->ram_avail_kb);
            }
            fclose(f);
        }
    }
#else
    hw->ram_total_kb = 0;
    hw->ram_avail_kb = 0;
#endif
    hw->is_low_memory = (hw->ram_total_kb < X12_LOW_RAM_KB);
}

static void x12_detect_gpu(X12HardwareProfile *hw) {
    /* Check for Vulkan device nodes */
    hw->has_vulkan   = (access("/dev/dri/renderD128", F_OK) == 0);
    hw->has_gpu      = hw->has_vulkan || (access("/dev/dri/card0", F_OK) == 0);
    hw->has_opengl_es3 = hw->has_gpu; /* assume ES3 if GPU present */

    /* Try to read GPU vendor from DRM */
    char vendor_path[64] = "/sys/class/drm/card0/device/vendor";
    char vendor_str[32]  = {0};
    if (x12_read_sysfs_str(vendor_path, vendor_str, sizeof(vendor_str))) {
        /* Map PCI vendor IDs */
        if (strstr(vendor_str, "0x14e4"))      strncpy(hw->gpu_vendor, "Broadcom", 32);
        else if (strstr(vendor_str, "0x1002")) strncpy(hw->gpu_vendor, "AMD",      32);
        else if (strstr(vendor_str, "0x10de")) strncpy(hw->gpu_vendor, "NVIDIA",   32);
        else if (strstr(vendor_str, "0x8086")) strncpy(hw->gpu_vendor, "Intel",    32);
        else                                   strncpy(hw->gpu_vendor, "Unknown",  32);
    } else {
        /* Qualcomm / ARM Mali / Imagination on Android */
        strncpy(hw->gpu_vendor, "Mobile-GPU", 32);
    }

    /* Display refresh rate */
    hw->display_hz = 60; /* safe default */
    const char *hz_paths[] = {
        "/sys/class/drm/card0-DSI-1/modes",
        "/sys/class/graphics/fb0/modes",
        NULL
    };
    for (int i = 0; hz_paths[i]; i++) {
        FILE *f = fopen(hz_paths[i], "r");
        if (f) {
            char line[64];
            if (fgets(line, sizeof(line), f)) {
                /* Format: U:1080x2340p-120 */
                char *at = strrchr(line, '-');
                if (at) hw->display_hz = atoi(at + 1);
            }
            fclose(f);
            break;
        }
    }
}

static int x12_read_thermal(void) {
    int temp_mc = x12_read_sysfs_int(SYSFS_THERMAL_ZONE, -1);
    if (temp_mc < 0) return 0;          /* unknown → assume cool */
    int temp_c = temp_mc / 1000;
    if (temp_c >= X12_THERMAL_THROTTLE_TEMP) return 3; /* throttling */
    if (temp_c >= 70)  return 2;         /* hot */
    if (temp_c >= 50)  return 1;         /* warm */
    return 0;                            /* cool */
}

static X12HardwareProfile x12_detect_hardware(void) {
    X12HardwareProfile hw = {0};
    x12_detect_cpu(&hw);
    x12_detect_memory(&hw);
    x12_detect_gpu(&hw);
    hw.thermal_level = x12_read_thermal();
    return hw;
}

/* ── Automatic mode selection — the adaptive formula ─────────────────── */
/*
 *  Decision matrix:
 *
 *  Input signals:
 *    T = thermal_level   (0–3)
 *    M = is_low_memory
 *    B = battery %       (-1 = unknown/plugged)
 *    P = is_plugged_in
 *    C = cpu_perf_cores
 *    G = has_gpu
 *
 *  Rules (evaluated top-to-bottom, first match wins):
 *    T >= 3               → POWER_SAVE   (device throttling)
 *    M == true            → POWER_SAVE   (< 2 GB RAM)
 *    B >= 0 && B < 20     → POWER_SAVE   (battery critical)
 *    T >= 2               → BALANCED     (hot but not throttling)
 *    B >= 0 && B < 40     → BALANCED     (low battery)
 *    P && T <= 1 && C >= 6→ TURBO        (plugged, cool, many cores)
 *    P || T == 0          → PERFORMANCE  (plugged or cool)
 *    default              → BALANCED
 */
static X12PerfMode x12_select_mode(const X12HardwareProfile *hw) {
    int  thermal = hw->thermal_level;
    bool low_mem = hw->is_low_memory;

    /* Read battery */
    int  battery  = x12_read_sysfs_int(SYSFS_BATTERY_CAP, -1);
    char bstat[32] = {0};
    x12_read_sysfs_str(SYSFS_BATTERY_STAT, bstat, sizeof(bstat));
    bool plugged = (strncasecmp(bstat, "Charging", 8) == 0 ||
                    strncasecmp(bstat, "Full",     4) == 0  ||
                    battery < 0);  /* no battery node = likely USB power */

    /* Apply decision matrix */
    if (thermal >= 3)                              return X12_MODE_POWER_SAVE;
    if (low_mem)                                   return X12_MODE_POWER_SAVE;
    if (battery >= 0 && battery < 20)             return X12_MODE_POWER_SAVE;
    if (thermal >= 2)                              return X12_MODE_BALANCED;
    if (battery >= 0 && battery < 40 && !plugged) return X12_MODE_BALANCED;
    if (plugged && thermal <= 1 && hw->cpu_perf_cores >= 6)
                                                   return X12_MODE_TURBO;
    if (plugged || thermal == 0)                   return X12_MODE_PERFORMANCE;
    return X12_MODE_BALANCED;
}

/* ── Render backend selection ─────────────────────────────────────────── */
static X12RenderHint x12_select_render(const X12HardwareProfile *hw,
                                        const X12Config          *cfg,
                                        X12PerfMode               mode) {
    if (cfg->render_hint != X12_RENDER_AUTO)
        return cfg->render_hint;

    /* Games → Vulkan > GLES > CPU */
    if (cfg->pkg_type == X12_PKG_GAME) {
        if (hw->has_vulkan && mode >= X12_MODE_PERFORMANCE) return X12_RENDER_VULKAN;
        if (hw->has_opengl_es3)  return X12_RENDER_GLES;
        return X12_RENDER_CPU;
    }
    /* Media → GLES for hardware decode */
    if (cfg->pkg_type == X12_PKG_MEDIA) {
        if (hw->has_opengl_es3)  return X12_RENDER_GLES;
        return X12_RENDER_CPU;
    }
    /* GUI apps → GLES if available, else XRender */
    if (cfg->pkg_type == X12_PKG_GUI_APP ||
        cfg->pkg_type == X12_PKG_BROWSER ||
        cfg->pkg_type == X12_PKG_IDE) {
        if (hw->has_opengl_es3 && mode >= X12_MODE_BALANCED) return X12_RENDER_GLES;
        return X12_RENDER_XRENDER;
    }
    /* WM/terminals → lightweight XRender */
    if (cfg->pkg_type == X12_PKG_WM || cfg->pkg_type == X12_PKG_TERMINAL)
        return X12_RENDER_XRENDER;

    /* Everything else */
    if (hw->has_opengl_es3) return X12_RENDER_GLES;
    return X12_RENDER_CPU;
}

/* ── Background monitor thread ───────────────────────────────────────── */
static void *x12_monitor_fn(void *arg) {
    X12Engine *eng = (X12Engine *)arg;
    while (eng->monitor_running) {
        struct timespec sleep_ts = { .tv_sec = X12_MONITOR_INTERVAL_MS / 1000,
                                     .tv_nsec = (X12_MONITOR_INTERVAL_MS % 1000) * 1000000L };
        nanosleep(&sleep_ts, NULL);

        /* Re-read dynamic signals */
        int new_thermal = x12_read_thermal();

        pthread_mutex_lock(&eng->lock);
        eng->hw.thermal_level = new_thermal;

        /* Re-evaluate RAM */
#if defined(__linux__) || defined(__ANDROID__)
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            eng->hw.ram_avail_kb = (uint64_t)si.freeram * si.mem_unit / 1024ULL;
        }
#endif

        /* Re-run mode selection */
        X12PerfMode new_mode = x12_select_mode(&eng->hw);
        if (new_mode != eng->mode) {
            if (eng->cfg.verbose) {
                fprintf(stderr, "[X12] mode change: %d → %d (thermal=%d)\n",
                        eng->mode, new_mode, new_thermal);
            }
            eng->mode = new_mode;
            eng->render_backend = x12_select_render(&eng->hw, &eng->cfg, eng->mode);
            /* Adjust frame budget */
            int hz = eng->hw.display_hz > 0 ? eng->hw.display_hz : 60;
            if (eng->mode == X12_MODE_POWER_SAVE) hz = hz / 2;
            eng->frame_budget_us = 1000000ULL / (uint64_t)hz;
        }
        pthread_mutex_unlock(&eng->lock);
    }
    return NULL;
}

/* ── Public API implementation ────────────────────────────────────────── */

X12Engine *x12_init(const X12Config *cfg) {
    X12Engine *eng = (X12Engine *)calloc(1, sizeof(X12Engine));
    if (!eng) return NULL;

    /* Copy config (use defaults if NULL) */
    if (cfg) {
        memcpy(&eng->cfg, cfg, sizeof(X12Config));
    } else {
        eng->cfg.pkg_type    = X12_PKG_GENERIC;
        eng->cfg.render_hint = X12_RENDER_AUTO;
    }

    pthread_mutex_init(&eng->lock, NULL);

    /* Detect hardware */
    eng->hw = x12_detect_hardware();

    /* Select initial mode */
    eng->mode           = x12_select_mode(&eng->hw);
    eng->render_backend = x12_select_render(&eng->hw, &eng->cfg, eng->mode);

    /* Frame budget */
    int hz = eng->hw.display_hz > 0 ? eng->hw.display_hz : 60;
    if (eng->mode == X12_MODE_POWER_SAVE) hz = hz / 2;
    eng->frame_budget_us = 1000000ULL / (uint64_t)hz;

    /* Start monitor thread */
    eng->monitor_running = true;
    if (pthread_create(&eng->monitor_thread, NULL, x12_monitor_fn, eng) != 0) {
        pthread_mutex_destroy(&eng->lock);
        free(eng);
        return NULL;
    }

    if (eng->cfg.verbose) {
        x12_dump_status(eng);
    }
    return eng;
}

void x12_shutdown(X12Engine *eng) {
    if (!eng) return;
    if (eng->monitor_running) {
        eng->monitor_running = false;
        pthread_join(eng->monitor_thread, NULL);
    }
    pthread_mutex_destroy(&eng->lock);
    free(eng);
}

const X12HardwareProfile *x12_get_hw_profile(const X12Engine *eng) {
    return eng ? &eng->hw : NULL;
}

X12PerfMode x12_get_perf_mode(const X12Engine *eng) {
    return eng ? eng->mode : X12_MODE_BALANCED;
}

void x12_frame_begin(X12Engine *eng) {
    if (!eng) return;
    clock_gettime(CLOCK_MONOTONIC, &eng->frame_start);
}

void x12_frame_end(X12Engine *eng) {
    if (!eng) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    uint64_t elapsed_us =
        (uint64_t)(now.tv_sec  - eng->frame_start.tv_sec)  * 1000000ULL +
        (uint64_t)(now.tv_nsec - eng->frame_start.tv_nsec) / 1000ULL;

    /* Record in ring buffer */
    eng->frame_times_us[eng->frame_idx % X12_FRAME_HISTORY] = elapsed_us;
    eng->frame_idx++;

    /* Frame pacing: sleep if we finished early */
    pthread_mutex_lock(&eng->lock);
    uint64_t budget = eng->frame_budget_us;
    pthread_mutex_unlock(&eng->lock);

    if (elapsed_us < budget) {
        uint64_t sleep_us = budget - elapsed_us;
        struct timespec sleep_ts = {
            .tv_sec  = (time_t)(sleep_us / 1000000ULL),
            .tv_nsec = (long)((sleep_us % 1000000ULL) * 1000ULL)
        };
        nanosleep(&sleep_ts, NULL);
    }
}

void x12_hint_heavy_work(X12Engine *eng) {
    if (!eng) return;
    /* On Linux we could set thread affinity to perf cores here.
     * In Termux we set scheduler hint via nice/sched_setaffinity. */
    (void)eng;
}

void x12_hint_idle(X12Engine *eng) {
    if (!eng) return;
    (void)eng;
}

int x12_get_optimal_threads(const X12Engine *eng) {
    if (!eng) return 2;
    pthread_mutex_lock((pthread_mutex_t *)&eng->lock);
    int cores = eng->hw.cpu_perf_cores;
    X12PerfMode mode = eng->mode;
    pthread_mutex_unlock((pthread_mutex_t *)&eng->lock);

    switch (mode) {
        case X12_MODE_POWER_SAVE:  return (cores > 2) ? 2 : 1;
        case X12_MODE_BALANCED:    return (cores > 1) ? cores / 2 : 1;
        case X12_MODE_PERFORMANCE: return cores;
        case X12_MODE_TURBO:       return cores + eng->hw.cpu_eff_cores;
        default:                   return cores;
    }
}

bool x12_should_use_simd(const X12Engine *eng, size_t data_bytes) {
    if (!eng) return false;
    /* SIMD overhead only worth it above ~4 KB */
    return eng->hw.has_neon && (data_bytes >= 4096);
}

X12RenderHint x12_get_render_backend(const X12Engine *eng) {
    if (!eng) return X12_RENDER_CPU;
    return eng->render_backend;
}

void x12_dump_status(const X12Engine *eng) {
    if (!eng) return;
    static const char *mode_names[] = {
        "POWER_SAVE", "BALANCED", "PERFORMANCE", "TURBO"
    };
    static const char *render_names[] = {
        "AUTO", "CPU", "GLES", "VULKAN", "XRENDER"
    };
    const X12HardwareProfile *hw = &eng->hw;
    fprintf(stderr,
        "[X12 Engine v" X12_ENGINE_VERSION_STR "]\n"
        "  Package  : %s\n"
        "  CPU      : %s (%d perf + %d eff cores @ %.1f GHz)\n"
        "  SIMD     : neon=%d sve=%d dotprod=%d\n"
        "  RAM      : %llu MB total, %llu MB avail%s\n"
        "  GPU      : %s  OpenGLES3=%d  Vulkan=%d  @%dHz\n"
        "  Thermal  : level %d\n"
        "  Mode     : %s\n"
        "  Render   : %s\n"
        "  Threads  : %d optimal\n",
        eng->cfg.package_name ? eng->cfg.package_name : "unknown",
        hw->cpu_arch, hw->cpu_perf_cores, hw->cpu_eff_cores,
        (double)hw->cpu_freq_max_khz / 1e6,
        hw->has_neon, hw->has_sve, hw->has_dotprod,
        (unsigned long long)(hw->ram_total_kb / 1024),
        (unsigned long long)(hw->ram_avail_kb / 1024),
        hw->is_low_memory ? " [LOW]" : "",
        hw->gpu_vendor, hw->has_opengl_es3, hw->has_vulkan, hw->display_hz,
        hw->thermal_level,
        mode_names[eng->mode],
        render_names[eng->render_backend],
        x12_get_optimal_threads(eng)
    );
}
