#!/usr/bin/env python3
"""
X12 Package Enhancement System
================================
Generates enhanced build.sh files for all 952 x11-packages.
Each package gets:
  - X12 performance engine injected into build flags
  - Version bumped with +x12 suffix
  - Binary wrapped with x12-wrap launcher
  - Optimal build system arguments (cmake/meson/autotools)
  - Package-type-specific tuning

Package type classification is done by name/keyword matching.
Build system detection is based on known upstream build systems.
"""

import os
import re
import json
import textwrap
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

# ── Package type classification ──────────────────────────────────────────────

PKG_TYPE_RULES = [
    # Games / emulators
    ("game",     ["doom", "quake", "openarena", "xonotic", "supertux",
                  "chocolate-doom", "dosbox", "bochs", "1oom", "cutechess",
                  "scummvm", "freeciv", "wesnoth", "minetest", "openttd",
                  "box2d", "sdl"]),
    # Window managers
    ("wm",       ["awesome", "bspwm", "dwm", "i3", "openbox", "xfwm",
                  "metacity", "kwin", "mutter", "compiz", "fluxbox",
                  "2bwm", "cinnamon", "muffin", "xmonad", "herbstluftwm",
                  "ratpoison", "sawfish", "enlightenment", "fvwm"]),
    # Terminal emulators
    ("terminal", ["alacritty", "aterm", "xterm", "konsole", "yakuake",
                  "rxvt", "kitty", "tilix", "terminus", "st", "cool-retro",
                  "guake", "tilda", "lxterminal", "mate-terminal",
                  "gnome-terminal", "xfce4-terminal"]),
    # Browsers
    ("browser",  ["chromium", "firefox", "epiphany", "falkon", "midori",
                  "browsh", "carbonyl", "angelfish", "dillo", "links",
                  "surf", "luakit", "qutebrowser", "otter-browser"]),
    # Media players / audio / video
    ("media",    ["vlc", "mpv", "audacious", "deadbeef", "audacity",
                  "ardour", "celluloid", "amberol", "audiotube", "clementine",
                  "rhythmbox", "totem", "parole", "gmusicbrowser",
                  "lmms", "hydrogen", "rosegarden", "muse"]),
    # IDEs / editors / code tools
    ("ide",      ["code-oss", "codelldb", "codeblocks", "bluefish",
                  "gedit", "kate", "kwrite", "mousepad", "leafpad",
                  "geany", "emacs", "vim-gtk", "neovim", "sublime",
                  "vscodium", "atom", "brackets", "eclipse"]),
    # Image / graphics
    ("media",    ["gimp", "inkscape", "darktable", "krita", "rawtherapee",
                  "gthumb", "eog", "eom", "shotwell", "digikam",
                  "azpainter", "drawy", "feh", "sxiv", "xviewer",
                  "nomacs", "gwenview"]),
    # GUI apps (catch-all for desktop apps)
    ("gui",      ["gtk", "qt", "kde", "gnome", "xfce", "mate", "lxde",
                  "cinnamon", "plasma", "breeze", "adwaita", "arc-",
                  "thunar", "nautilus", "dolphin", "nemo", "caja",
                  "pcmanfm", "krusader", "ranger"]),
    # Libraries
    ("library",  ["lib", "atk", "cogl", "clutter", "glib", "pango",
                  "cairo", "dconf", "dbus", "bamf", "ibus", "fcitx",
                  "ayatana", "akonadi", "calendarsupport", "at-spi",
                  "appstream", "baloo", "analitza", "eventviews",
                  "kio", "solid", "kconfig", "kservice"]),
]

# Build system classification
BUILD_SYSTEM_MAP = {
    # cmake
    "cmake": [
        "dolphin", "kate", "ark", "okular", "konsole", "spectacle",
        "gwenview", "krita", "digikam", "falkon", "breeze", "plasma",
        "kwin", "kservice", "kconfig", "kio", "solid", "baloo",
        "akonadi", "calendarsupport", "analitza", "cantor", "calligra",
        "artikulate", "blinken", "crow-translate", "cherrytree",
        "cutechess", "dbeaver", "darktable", "eventviews", "kmail",
        "kaddressbook", "kalarm", "korganizer", "akregator",
        "angelfish", "audiotube", "arianna", "alligator", "amberol",
    ],
    # meson
    "meson": [
        "amberol", "epiphany", "gnome", "glib", "gtk", "pango",
        "cairo", "dconf", "mutter", "nautilus", "gedit", "eog",
        "totem", "rhythmbox", "celluloid", "clutter", "cogl",
        "atk", "at-spi2", "adwaita", "appstream-glib", "dunst",
        "eww", "cuse",
    ],
    # autotools (default)
    "autotools": [],
}

# Per-package binary name overrides (package_name → binary_path)
BINARY_OVERRIDES = {
    "alacritty":     "bin/alacritty",
    "awesome":       "bin/awesome",
    "bspwm":         "bin/bspwm",
    "chromium":      "bin/chromium",
    "code-oss":      "bin/code-oss",
    "dwm":           "bin/dwm",
    "emacs-x":       "bin/emacs",
    "epiphany":      "bin/epiphany",
    "falkon":        "bin/falkon",
    "gimp":          "bin/gimp",
    "inkscape":      "bin/inkscape",
    "konsole":       "bin/konsole",
    "krita":         "bin/krita",
    "mpv":           "bin/mpv",
    "vlc":           "bin/vlc",
    "audacious":     "bin/audacious",
    "audacity":      "bin/audacity",
    "ardour":        "bin/ardour",
    "deadbeef":      "bin/deadbeef",
    "darktable":     "bin/darktable",
    "dolphin":       "bin/dolphin",
    "codeblocks":    "bin/codeblocks",
    "bluefish":      "bin/bluefish",
    "celluloid":     "bin/celluloid",
    "cherrytree":    "bin/cherrytree",
    "cinnamon":      "bin/cinnamon",
    "chocolate-doom":"bin/chocolate-doom",
    "dosbox":        "bin/dosbox",
    "dosbox-x":      "bin/dosbox-x",
    "1oom":          "bin/1oom",
    "cutechess":     "bin/cutechess",
}

# Per-package extra cmake args
EXTRA_CMAKE_ARGS = {
    "chromium":  ["-DUSE_AURA=1", "-DUSE_OZONE=0"],
    "krita":     ["-DHIDE_SAFE_ASSERTS=ON", "-DBUILD_TESTING=OFF"],
    "darktable": ["-DRAWSPEED_ENABLE_LTO=ON"],
    "inkscape":  ["-DWITH_OPENMP=ON"],
    "ardour":    [],
}

# Per-package extra configure args
EXTRA_CONFIGURE_ARGS = {
    "audacious":  ["--enable-gtk3", "--enable-gapless-playback"],
    "deadbeef":   ["--enable-pulse", "--enable-alsa"],
    "vlc":        ["--enable-optimizations", "--enable-run-as-root"],
    "dosbox":     ["--enable-core-inline", "--enable-fpu-x86"],
}


@dataclass
class PackageSpec:
    name: str
    pkg_type: str = "generic"
    build_system: str = "autotools"
    binary_path: str = ""
    extra_cmake: list = field(default_factory=list)
    extra_configure: list = field(default_factory=list)
    description: str = ""
    homepage: str = ""


def classify_pkg_type(name: str) -> str:
    name_lower = name.lower()
    for pkg_type, keywords in PKG_TYPE_RULES:
        if any(kw in name_lower for kw in keywords):
            return pkg_type
    return "generic"


def classify_build_system(name: str) -> str:
    name_lower = name.lower()
    for system, names in BUILD_SYSTEM_MAP.items():
        if any(n in name_lower for n in names):
            return system
    # Heuristics: KDE packages → cmake, GNOME → meson, rest → autotools
    if any(k in name_lower for k in ["kde", "plasma", "kf5", "kf6",
                                      "akonadi", "calligra", "kmail"]):
        return "cmake"
    if any(k in name_lower for k in ["gnome", "glib", "gtk", "gdk"]):
        return "meson"
    return "autotools"


def make_pkg_spec(name: str) -> PackageSpec:
    spec = PackageSpec(name=name)
    spec.pkg_type     = classify_pkg_type(name)
    spec.build_system = classify_build_system(name)
    spec.binary_path  = BINARY_OVERRIDES.get(name, f"bin/{name}")
    spec.extra_cmake  = EXTRA_CMAKE_ARGS.get(name, [])
    spec.extra_configure = EXTRA_CONFIGURE_ARGS.get(name, [])
    return spec


# ── Build.sh template generators ─────────────────────────────────────────────

def gen_cmake_build_sh(spec: PackageSpec) -> str:
    extra = "\n".join(f'        "{a}" \\' for a in spec.extra_cmake)
    extra_block = f"\n{extra}" if extra else ""
    return f"""\
TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="{spec.name} — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${{TERMUX_PKG_VERSION}}+x12"
TERMUX_PKG_SRCURL="https://github.com/termux/termux-packages/tree/master/x11-packages/{spec.name}"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${{TERMUX_PKG_BUILDER_DIR}}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "{spec.pkg_type}"

termux_step_configure() {{
    termux_setup_cmake
    cmake \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \\
        -DCMAKE_C_FLAGS="${{X12_CFLAGS}}" \\
        -DCMAKE_CXX_FLAGS="${{X12_CXXFLAGS}}" \\
        -DCMAKE_EXE_LINKER_FLAGS="${{X12_LDFLAGS}}" \\
        -DCMAKE_SHARED_LINKER_FLAGS="${{X12_LDFLAGS}}" \\
        -DBUILD_TESTING=OFF \\
        -DBUILD_DOCS=OFF \\
        -DBUILD_EXAMPLES=OFF \\
        -DENABLE_TESTS=OFF \\
        -DX12_ENGINE=ON \\{extra_block}
        "${{TERMUX_PKG_SRCDIR}}"
}}

termux_step_make() {{
    make -j"${{TERMUX_MAKE_PROCESSES}}"
}}

termux_step_make_install() {{
    make install
    x12_post_install "{spec.binary_path}" "{spec.pkg_type}"
    x12_bump_version
}}
"""


def gen_meson_build_sh(spec: PackageSpec) -> str:
    return f"""\
TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="{spec.name} — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${{TERMUX_PKG_VERSION}}+x12"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${{TERMUX_PKG_BUILDER_DIR}}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "{spec.pkg_type}"

termux_step_configure() {{
    x12_inject_meson
    meson setup \\
        --buildtype=release \\
        --optimization=3 \\
        -Db_lto=true \\
        -Db_lto_mode=thin \\
        -Db_ndebug=true \\
        -Db_pie=true \\
        "${{TERMUX_PKG_SRCDIR}}" .
}}

termux_step_make() {{
    ninja -j"${{TERMUX_MAKE_PROCESSES}}"
}}

termux_step_make_install() {{
    ninja install
    x12_post_install "{spec.binary_path}" "{spec.pkg_type}"
    x12_bump_version
}}
"""


def gen_autotools_build_sh(spec: PackageSpec) -> str:
    extra_args = " ".join(spec.extra_configure)
    extra_line = f"\n        {extra_args} \\" if extra_args else ""
    return f"""\
TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="{spec.name} — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${{TERMUX_PKG_VERSION}}+x12"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${{TERMUX_PKG_BUILDER_DIR}}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "{spec.pkg_type}"

termux_step_configure() {{
    x12_inject_configure_args
    ./configure \\
        --prefix="${{TERMUX_PREFIX}}" \\
        --host="${{TERMUX_HOST_PLATFORM}}" \\
        --disable-debug \\
        --disable-tests \\
        --disable-docs \\
        --enable-optimizations \\{extra_line}
        "${{TERMUX_PKG_EXTRA_CONFIGURE_ARGS:-}}"
}}

termux_step_make() {{
    make -j"${{TERMUX_MAKE_PROCESSES}}"
}}

termux_step_make_install() {{
    make install
    x12_post_install "{spec.binary_path}" "{spec.pkg_type}"
    x12_bump_version
}}
"""


def gen_build_sh(spec: PackageSpec) -> str:
    """Pick the right template for this package."""
    if spec.build_system == "cmake":
        return gen_cmake_build_sh(spec)
    elif spec.build_system == "meson":
        return gen_meson_build_sh(spec)
    else:
        return gen_autotools_build_sh(spec)


def generate_all_packages(
    pkg_names: list[str],
    output_dir: str,
    verbose: bool = True,
) -> dict:
    """Generate build.sh for all packages. Returns summary dict."""
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    stats = {
        "total": len(pkg_names),
        "cmake": 0, "meson": 0, "autotools": 0,
        "by_type": {},
        "packages": [],
    }

    for i, name in enumerate(pkg_names):
        spec = make_pkg_spec(name)

        # Create package directory
        pkg_dir = output_path / name
        pkg_dir.mkdir(exist_ok=True)

        # Write build.sh
        build_sh_content = gen_build_sh(spec)
        (pkg_dir / "build.sh").write_text(build_sh_content)

        # Update stats
        stats[spec.build_system] += 1
        stats["by_type"][spec.pkg_type] = stats["by_type"].get(spec.pkg_type, 0) + 1
        stats["packages"].append({
            "name": name,
            "type": spec.pkg_type,
            "build_system": spec.build_system,
            "binary": spec.binary_path,
        })

        if verbose and (i + 1) % 100 == 0:
            print(f"  Generated {i+1}/{len(pkg_names)} packages...")

    return stats


if __name__ == "__main__":
    import sys
    # Read package list
    pkg_file = Path("/tmp/x11_packages.txt")
    if not pkg_file.exists():
        print("ERROR: /tmp/x11_packages.txt not found. Run the fetch step first.")
        sys.exit(1)

    pkg_names = [l.strip() for l in pkg_file.read_text().splitlines() if l.strip()]
    print(f"Generating {len(pkg_names)} x12 package build.sh files...")

    stats = generate_all_packages(
        pkg_names,
        output_dir="/root/x12-repo/packages",
        verbose=True,
    )

    print(f"\nDone!")
    print(f"  Total    : {stats['total']}")
    print(f"  CMake    : {stats['cmake']}")
    print(f"  Meson    : {stats['meson']}")
    print(f"  Autotools: {stats['autotools']}")
    print(f"  By type  : {stats['by_type']}")

    # Save stats
    Path("/root/x12-repo/packages/_stats.json").write_text(
        json.dumps(stats, indent=2)
    )
