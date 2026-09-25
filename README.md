# Project X12-Repo

> **All 952 Termux x11-packages, rebuilt with the X12 Adaptive Performance Engine.**
> Every package runs faster, smoother, adapts to your hardware, and configures itself — automatically.

---

## What Is X12-Repo?

X12-Repo is a drop-in replacement for the Termux `x11-packages` repository.
Every package has been cloned from the upstream Termux x11-packages repo and
rebuilt with the **X12 Adaptive Performance Engine** injected into the build.

The packages are named `<package>-x12` (e.g. `alacritty-x12`, `dolphin-x12`)
so they install alongside the originals without conflicts.

---

## The Four Pillars

Every x12 package delivers four guarantees — all automatic, zero configuration:

| Pillar | What it does |
|---|---|
| **Speed** | Built with `-O3 -flto=thin` + dead-code elimination + arch-tuned SIMD flags (`-march=armv8-a+dotprod` on aarch64). LTO links the entire program as one unit, eliminating cross-module overhead. |
| **Smoothness** | The `x12-wrap` launcher sets frame-pacing, chooses the best render backend (Vulkan → OpenGL ES → XRender → CPU), and configures GTK/Qt/Clutter for minimal jank. |
| **Adaptive** | `libx12engine` runs a hardware probe at startup: counts CPU big/LITTLE cores, reads available RAM, detects GPU capabilities and display refresh rate. It picks codepaths specifically tuned for *your* device. |
| **Automatic** | A background monitor thread re-evaluates the device state every 2 seconds. If the phone heats up, it drops to POWER_SAVE. If you plug in the charger and it cools down, it jumps to PERFORMANCE or TURBO. No user action needed. |

---

## Performance Mode Decision Matrix

The engine uses this logic on every evaluation cycle:

```
IF   thermal ≥ 80°C                  → POWER_SAVE
ELIF RAM < 2 GB                       → POWER_SAVE
ELIF battery < 20%                    → POWER_SAVE
ELIF thermal ≥ 70°C                   → BALANCED
ELIF battery < 40% AND not charging   → BALANCED
ELIF plugged + cool + ≥6 perf cores   → TURBO
ELIF plugged OR cool                   → PERFORMANCE
ELSE                                  → BALANCED
```

---

## Package Stats

| Build System | Count |
|---|---|
| Autotools (`./configure`) | 731 |
| CMake | 151 |
| Meson | 70 |
| **Total** | **952** |

| Package Type | Count |
|---|---|
| Generic GUI / X11 | 472 |
| GUI Apps (GTK/Qt) | 228 |
| Libraries | 105 |
| Terminal Emulators | 43 |
| IDEs / Editors | 18 |
| Media (audio/video) | 27 |
| Games / Emulators | 28 |
| Window Managers | 21 |
| Browsers | 10 |

---

## Quick Install

Run this inside Termux:

```bash
curl -fsSL https://raw.githubusercontent.com/x12-repo/x12-repo/main/scripts/setup-x12-repo.sh | bash
```

Then install any x12-enhanced package:

```bash
pkg install alacritty-x12      # Terminal emulator
pkg install dolphin-x12        # File manager
pkg install chromium-x12       # Web browser
pkg install vlc-x12            # Media player
pkg install awesome-x12        # Window manager
pkg install krita-x12          # Digital painting
pkg install deadbeef-x12       # Music player
pkg install dosbox-x12         # DOS emulator
```

---

## Repository Structure

```
x12-repo/
│
├── engine/                         # X12 Adaptive Performance Engine
│   ├── include/x12_engine.h        # Public API header
│   ├── src/x12_engine.c            # Core engine (hardware detection,
│   │                               #   mode selection, frame pacing)
│   ├── src/x12_info.c              # Diagnostic tool
│   ├── Makefile                    # Builds libx12engine.so + x12-info
│   └── scripts/
│       ├── x12-wrap                # Runtime launcher wrapper (bash)
│       └── x12-build-flags.sh     # Build flag injector (sourced by build.sh)
│
├── packages/                       # 952 x12-enhanced package definitions
│   ├── _stats.json                 # Build stats (type, build system counts)
│   ├── alacritty/build.sh          # Example: terminal / autotools
│   ├── dolphin/build.sh            # Example: gui / cmake
│   ├── epiphany/build.sh           # Example: browser / meson
│   └── ... (952 total)
│
├── repo/                           # APT repository
│   ├── packages.json               # Full package database (953 entries)
│   └── dists/x12/
│       ├── Release                 # APT Release file
│       └── main/
│           ├── binary-aarch64/Packages
│           ├── binary-arm/Packages
│           └── binary-x86_64/Packages
│
├── scripts/
│   ├── generate_packages.py        # Generates all 952 build.sh files
│   ├── build_repo.py               # Builds APT repo indexes
│   └── setup-x12-repo.sh          # User install script
│
├── ci/
│   ├── orchestrator.py             # Parallel build driver
│   └── build_report.json           # Last build results
│
└── .github/workflows/
    └── build.yml                   # GitHub Actions CI
```

---

## How the Engine Works

### 1. Hardware Detection (`x12_detect_hardware`)

At startup `libx12engine` reads:
- `/proc/cpuinfo` — CPU model, core count
- `/sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq` — per-core frequencies
  (used to split big/LITTLE cores)
- `getauxval(AT_HWCAP)` — NEON, SVE, dot-product SIMD flags on ARM
- `/proc/meminfo` — total and available RAM
- `/dev/dri/renderD128` — Vulkan GPU presence
- `/sys/class/thermal/thermal_zone0/temp` — CPU temperature
- `/sys/class/power_supply/battery/capacity` — battery level
- `/sys/class/power_supply/battery/status` — charging state

### 2. Mode Selection (automatic, re-runs every 2 seconds)

The background monitor thread continuously re-evaluates the mode using the
decision matrix above and switches modes without any user interaction.

### 3. Per-Package Tuning

Each package is classified into one of 10 types at build time:

| Type | Render backend | Extra flags |
|---|---|---|
| `game` | Vulkan → GLES → CPU | `-ffast-math -funroll-loops` |
| `media` | GLES → CPU | `-ffast-math -fvectorize` |
| `browser` | GLES → XRender | `-O2` (browsers have own LTO) |
| `wm` | XRender | `-fstack-protector-strong` |
| `terminal` | XRender | `-fstack-protector-strong` |
| `gui` | GLES → XRender | base flags |
| `ide` | GLES → XRender | base flags |
| `library` | — | thin LTO |
| `generic` | GLES → CPU | base flags |

### 4. Runtime Wrapper (`x12-wrap`)

Every x12 binary is launched through `x12-wrap`, which:
1. Reads current hardware state
2. Runs the mode selection matrix in shell
3. Sets `OMP_NUM_THREADS`, `GTK_*`, `QT_*`, `MESA_*`, `PULSE_LATENCY_MSEC`,
   `_JAVA_OPTIONS` and other env vars to match the chosen mode
4. Sets `LD_PRELOAD=libx12engine.so` so the C engine is active in-process
5. `exec`s the real binary

---

## Building From Source

### Prerequisites

```bash
# Inside Termux
pkg install clang lld make cmake ninja python3

# Or on a Linux host with Android NDK
export NDK=/path/to/android-ndk-r26d
```

### Build the engine

```bash
cd engine/
make TARGET_ARCH=aarch64 all
make install   # installs to $PREFIX
```

### Generate all package build scripts

```bash
python3 scripts/generate_packages.py
```

### Build a specific package

```bash
# With Termux build system installed:
python3 ci/orchestrator.py --packages alacritty dolphin --arch aarch64

# Dry run (shows what would happen):
python3 ci/orchestrator.py --dry-run
```

### Build everything

```bash
python3 ci/orchestrator.py --arch aarch64 --jobs 8
```

---

## Diagnostic Tool

After installing `libx12engine`, run `x12-info` to see what the engine
detects on your device:

```
X12 Adaptive Performance Engine v1.0.0
==========================================
[X12 Engine v1.0.0]
  Package  : x12-info
  CPU      : aarch64 (6 perf + 2 eff cores @ 2.8 GHz)
  SIMD     : neon=1 sve=0 dotprod=1
  RAM      : 8192 MB total, 4096 MB avail
  GPU      : Mobile-GPU  OpenGLES3=1  Vulkan=1  @120Hz
  Thermal  : level 0
  Mode     : PERFORMANCE
  Render   : GLES
  Threads  : 6 optimal
```

---

## Differences from x11-packages

| Feature | x11-packages | x12-repo |
|---|---|---|
| Compiler flags | `-O2` (default) | `-O3 -flto=thin -march=native` |
| LTO | No | Yes (thin LTO across all TUs) |
| SIMD | Compiler default | Explicit NEON/SVE/dotprod |
| Dead-code elimination | No | `--gc-sections --icf=safe` |
| Runtime adaptation | No | Auto mode switching every 2s |
| Frame pacing | App-dependent | Centralized in engine |
| Render backend | App-chosen | Engine-optimised per type |
| Binary wrapper | No | `x12-wrap` sets optimal env |
| Version suffix | none | `+x12` |
| Package name | `<pkg>` | `<pkg>-x12` |

---

## License

- X12 Engine source code: MIT
- Individual packages: inherit upstream license
- This repo infrastructure: MIT

---

## Credits

Built on top of the excellent work by the
[Termux](https://github.com/termux) team and the
[termux-packages](https://github.com/termux/termux-packages) contributors.

X12-Repo adds the performance layer on top — it does not modify
upstream source code, only the build flags, runtime wrapper, and
hardware-adaptive engine.
