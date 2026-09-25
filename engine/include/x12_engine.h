/*
 * X12 Adaptive Performance Engine
 * ================================
 * Project X12-Repo — Termux x11-packages enhanced fork
 *
 * Provides four guarantees for every package built against it:
 *   1. SPEED      — compiler-guided hot-path selection, O3+LTO builds
 *   2. SMOOTHNESS — frame pacing, render throttle, vsync-aware scheduling
 *   3. ADAPTIVE   — runtime CPU/GPU/RAM profiling → best codepath chosen
 *   4. AUTOMATIC  — zero user configuration; engine decides everything
 *
 * Usage:
 *   #include "x12_engine.h"
 *   // Call x12_init() before any app work; everything else is automatic.
 */

#ifndef X12_ENGINE_H
#define X12_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ── Version ──────────────────────────────────────────────────────────── */
#define X12_ENGINE_VERSION_MAJOR  1
#define X12_ENGINE_VERSION_MINOR  0
#define X12_ENGINE_VERSION_PATCH  0
#define X12_ENGINE_VERSION_STR    "1.0.0"

/* ── Hardware profile (filled by x12_detect_hardware) ────────────────── */
typedef struct {
    /* CPU */
    int      cpu_cores;
    int      cpu_perf_cores;     /* big cores on big.LITTLE */
    int      cpu_eff_cores;      /* LITTLE cores */
    uint64_t cpu_freq_max_khz;
    bool     has_neon;           /* ARM NEON SIMD */
    bool     has_sve;            /* ARM SVE  */
    bool     has_dotprod;        /* ARM dot-product */
    char     cpu_arch[32];       /* "aarch64", "arm", "x86_64" */
    char     cpu_model[128];

    /* Memory */
    uint64_t ram_total_kb;
    uint64_t ram_avail_kb;
    bool     is_low_memory;      /* < 2 GB RAM */

    /* GPU / display */
    bool     has_gpu;
    bool     has_opengl_es3;
    bool     has_vulkan;
    char     gpu_vendor[64];
    int      display_hz;         /* refresh rate, 0 = unknown */

    /* Thermal */
    int      thermal_level;      /* 0=cool 1=warm 2=hot 3=throttling */
} X12HardwareProfile;

/* ── Performance mode (auto-selected) ────────────────────────────────── */
typedef enum {
    X12_MODE_POWER_SAVE  = 0,   /* thermal throttle / low battery */
    X12_MODE_BALANCED    = 1,   /* default; good battery + perf */
    X12_MODE_PERFORMANCE = 2,   /* plugged in / high-perf request  */
    X12_MODE_TURBO       = 3,   /* maximum everything; short bursts */
} X12PerfMode;

/* ── Render hint (per-package type) ─────────────────────────────────────*/
typedef enum {
    X12_RENDER_AUTO      = 0,   /* engine decides */
    X12_RENDER_CPU       = 1,   /* force software render */
    X12_RENDER_GLES      = 2,   /* OpenGL ES */
    X12_RENDER_VULKAN    = 3,   /* Vulkan */
    X12_RENDER_XRENDER   = 4,   /* X11 XRender */
} X12RenderHint;

/* ── Package type (guides adaptive decisions) ────────────────────────── */
typedef enum {
    X12_PKG_GENERIC      = 0,
    X12_PKG_GUI_APP      = 1,   /* GTK/Qt desktop app */
    X12_PKG_GAME         = 2,   /* game / realtime render */
    X12_PKG_MEDIA        = 3,   /* audio/video processing */
    X12_PKG_BROWSER      = 4,   /* Chromium/Firefox type */
    X12_PKG_IDE          = 5,   /* editor / IDE */
    X12_PKG_WM           = 6,   /* window manager */
    X12_PKG_TERMINAL     = 7,   /* terminal emulator */
    X12_PKG_LIBRARY      = 8,   /* shared library */
    X12_PKG_TOOL         = 9,   /* CLI tool */
} X12PkgType;

/* ── Main engine config ──────────────────────────────────────────────── */
typedef struct {
    const char    *package_name;
    X12PkgType     pkg_type;
    X12RenderHint  render_hint;

    /* Tuning knobs (0 = auto) */
    int            thread_pool_size;   /* 0 = auto from cpu_perf_cores */
    int            frame_target_hz;    /* 0 = match display_hz          */
    bool           enable_jit_hints;   /* feed branch-prediction hints  */
    bool           enable_prealloc;    /* pre-fault memory pages        */
    bool           verbose;
} X12Config;

/* ── Engine state (opaque to callers) ────────────────────────────────── */
typedef struct X12Engine X12Engine;

/* ── Public API ──────────────────────────────────────────────────────── */

/**
 * x12_init — initialise the engine.
 * Detects hardware, selects performance mode, starts background monitor.
 * Returns engine handle; NULL on failure.
 * If cfg is NULL a safe default config is used.
 */
X12Engine *x12_init(const X12Config *cfg);

/** Shut down engine cleanly. */
void x12_shutdown(X12Engine *eng);

/** Query current hardware profile. */
const X12HardwareProfile *x12_get_hw_profile(const X12Engine *eng);

/** Query currently active performance mode. */
X12PerfMode x12_get_perf_mode(const X12Engine *eng);

/**
 * x12_frame_begin / x12_frame_end
 * Call around each rendered frame.
 * Engine paces frame rate and adjusts quality/quality trade-offs.
 */
void x12_frame_begin(X12Engine *eng);
void x12_frame_end(X12Engine *eng);

/**
 * x12_hint_heavy_work / x12_hint_idle
 * Inform engine that heavy CPU work is starting/ending.
 * Engine boosts/relaxes thread affinity accordingly.
 */
void x12_hint_heavy_work(X12Engine *eng);
void x12_hint_idle(X12Engine *eng);

/**
 * x12_get_optimal_threads
 * Returns ideal thread count for a parallel work unit.
 */
int x12_get_optimal_threads(const X12Engine *eng);

/**
 * x12_should_use_simd
 * Returns true if SIMD acceleration is beneficial for given data size.
 */
bool x12_should_use_simd(const X12Engine *eng, size_t data_bytes);

/**
 * x12_get_render_backend
 * Returns the best render backend for this hardware+package combo.
 */
X12RenderHint x12_get_render_backend(const X12Engine *eng);

/** Human-readable status dump to stderr. */
void x12_dump_status(const X12Engine *eng);

/* ── Convenience macro: auto-init with package name + type ──────────── */
#define X12_AUTO_INIT(pkg_name, pkg_type) \
    do { \
        X12Config _x12cfg = {0}; \
        _x12cfg.package_name  = (pkg_name); \
        _x12cfg.pkg_type      = (pkg_type); \
        _x12cfg.render_hint   = X12_RENDER_AUTO; \
        _x12cfg.verbose       = false; \
        x12_init(&_x12cfg); \
    } while(0)

#ifdef __cplusplus
}
#endif
#endif /* X12_ENGINE_H */
