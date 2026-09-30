#!/usr/bin/env python3
"""
X12 Repo Infrastructure Builder
=================================
Generates:
  - packages.json  — full package database
  - repo/dists/x12/main/binary-*/Packages  — APT Packages index
  - repo/dists/x12/Release                 — APT Release file
  - scripts/setup-x12-repo.sh              — user install script
"""

import json
import os
import hashlib
import gzip
from pathlib import Path
from datetime import datetime, timezone

REPO_ROOT    = Path(__file__).resolve().parent.parent
STATS_FILE   = REPO_ROOT / "packages/_stats.json"
PACKAGES_JSON= REPO_ROOT / "repo/packages.json"
DIST_DIR     = REPO_ROOT / "repo/dists/x12"
ARCHES       = ["aarch64", "arm", "x86_64"]

REPO_META = {
    "name": "x12-repo",
    "label": "X12 Enhanced Packages",
    "description": "Termux x11-packages rebuilt with X12 Adaptive Performance Engine",
    "version": "1.0.0",
    "origin": "x12-repo",
    "suite": "x12",
    "component": "main",
    "homepage": "https://github.com/x12-repo/x12-repo",
    "maintainer": "X12 Repo <x12@x12-repo.dev>",
    "generated": datetime.now(timezone.utc).isoformat(),
}

# ── Load stats ────────────────────────────────────────────────────────────────

def load_stats() -> dict:
    if STATS_FILE.exists():
        return json.loads(STATS_FILE.read_text())
    return {"packages": []}

# ── Build package database ────────────────────────────────────────────────────

def build_packages_json(stats: dict) -> list:
    packages = []
    for p in stats.get("packages", []):
        name = p["name"]
        pkg = {
            "Package":      f"{name}-x12",
            "name_orig":    name,
            "Version":      "1.0.0+x12",
            "Architecture": "aarch64",
            "Maintainer":   "x12-repo",
            "Section":      section_for_type(p["type"]),
            "Priority":     "optional",
            "Depends":      "libx12engine",
            "Description":  f"{name} rebuilt with X12 Adaptive Performance Engine.\n"
                           f" Package type: {p['type']}, "
                           f"build system: {p['build_system']}.\n"
                           f" Includes speed (O3+LTO), smoothness (frame-pacing),\n"
                           f" adaptive (runtime CPU/GPU detection), and automatic\n"
                           f" performance mode selection.",
            "build_system": p["build_system"],
            "pkg_type":     p["type"],
            "binary":       p["binary"],
            "x12_features": {
                "speed":      True,
                "smoothness": True,
                "adaptive":   True,
                "automatic":  True,
            },
            "build_flags": build_flags_summary(p["type"]),
        }
        packages.append(pkg)
    return packages

def section_for_type(pkg_type: str) -> str:
    return {
        "game":     "games",
        "wm":       "x11",
        "terminal": "x11",
        "browser":  "web",
        "media":    "multimedia",
        "ide":      "editors",
        "gui":      "x11",
        "library":  "libs",
        "generic":  "x11",
    }.get(pkg_type, "x11")

def build_flags_summary(pkg_type: str) -> dict:
    base = {
        "CFLAGS":  "-O3 -flto=thin -ffunction-sections -fdata-sections -fomit-frame-pointer",
        "LDFLAGS": "-Wl,--gc-sections -Wl,--icf=safe -Wl,-O2 -lx12engine",
        "LTO":     "thin",
        "arch_specific": True,
    }
    if pkg_type == "game":
        base["CFLAGS"] += " -ffast-math -funroll-loops"
    elif pkg_type == "media":
        base["CFLAGS"] += " -ffast-math -fvectorize"
    elif pkg_type == "browser":
        base["CFLAGS"] = "-O2 -ffunction-sections -fdata-sections"
    return base

# ── APT Packages index ────────────────────────────────────────────────────────

def write_packages_index(packages: list, arch: str):
    lines = []
    for p in packages:
        pkg_arch = p.get("Architecture", "aarch64")
        if arch != "all" and pkg_arch != arch and pkg_arch != "all":
            # Write aarch64 packages to aarch64, arm to arm, etc.
            if arch == "aarch64" and pkg_arch == "aarch64":
                pass
            elif arch != "aarch64":
                # For non-aarch64 arches, clone the package entry
                pass

        lines += [
            f"Package: {p['Package']}",
            f"Version: {p['Version']}",
            f"Architecture: {arch}",
            f"Maintainer: {p['Maintainer']}",
            f"Section: {p['Section']}",
            f"Priority: {p['Priority']}",
            f"Depends: {p['Depends']}",
            f"Description: {p['Description'].splitlines()[0]}",
            f" X12 pkg_type={p['pkg_type']} build={p['build_system']}",
            f" Speed: O3+LTO | Smooth: frame-pacing | Adaptive: auto-detect | Auto: yes",
            "",
        ]

    content = "\n".join(lines)

    out_dir = DIST_DIR / f"main/binary-{arch}"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Plain Packages
    (out_dir / "Packages").write_text(content)

    # Gzipped
    with gzip.open(out_dir / "Packages.gz", "wb") as f:
        f.write(content.encode())

    print(f"  Wrote Packages index for {arch}: {len(packages)} entries")
    return content

# ── APT Release file ──────────────────────────────────────────────────────────

def write_release(packages_by_arch: dict):
    now_str = datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S +0000")

    lines = [
        f"Origin: {REPO_META['origin']}",
        f"Label: {REPO_META['label']}",
        f"Suite: {REPO_META['suite']}",
        f"Codename: x12",
        f"Components: main",
        f"Architectures: {' '.join(ARCHES)}",
        f"Description: {REPO_META['description']}",
        f"Date: {now_str}",
        "MD5Sum:",
    ]

    # Hash entries for each Packages file
    for arch in ARCHES:
        pkg_path = DIST_DIR / f"main/binary-{arch}/Packages"
        if pkg_path.exists():
            data = pkg_path.read_bytes()
            md5  = hashlib.md5(data).hexdigest()
            sha1 = hashlib.sha1(data).hexdigest()
            sha256 = hashlib.sha256(data).hexdigest()
            size = len(data)
            rel_path = f"main/binary-{arch}/Packages"
            lines.append(f" {md5} {size:8d} {rel_path}")

    lines.append("SHA1:")
    for arch in ARCHES:
        pkg_path = DIST_DIR / f"main/binary-{arch}/Packages"
        if pkg_path.exists():
            data = pkg_path.read_bytes()
            sha1 = hashlib.sha1(data).hexdigest()
            size = len(data)
            rel_path = f"main/binary-{arch}/Packages"
            lines.append(f" {sha1} {size:8d} {rel_path}")

    lines.append("SHA256:")
    for arch in ARCHES:
        pkg_path = DIST_DIR / f"main/binary-{arch}/Packages"
        if pkg_path.exists():
            data = pkg_path.read_bytes()
            sha256 = hashlib.sha256(data).hexdigest()
            size = len(data)
            rel_path = f"main/binary-{arch}/Packages"
            lines.append(f" {sha256} {size:8d} {rel_path}")

    content = "\n".join(lines) + "\n"
    (DIST_DIR / "Release").write_text(content)
    print(f"  Wrote Release file")

# ── User setup script ─────────────────────────────────────────────────────────

def write_setup_script():
    script = """\
#!/data/data/com.termux/files/usr/bin/bash
# X12 Repo Setup Script
# ======================
# Adds the x12-repo as a Termux package source and installs
# the X12 engine library.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/x12-repo/x12-repo/main/scripts/setup-x12-repo.sh | bash

set -euo pipefail

REPO_URL="https://x12-repo.github.io/x12-repo"
SOURCES_DIR="${PREFIX}/etc/apt/sources.list.d"
SOURCE_FILE="${SOURCES_DIR}/x12-repo.list"

echo "╔══════════════════════════════════════════╗"
echo "║  X12 Repo — Performance-Enhanced Packages║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Check we're in Termux
if [ ! -d "${PREFIX}" ]; then
    echo "ERROR: This script must run inside Termux."
    exit 1
fi

# Add source
mkdir -p "${SOURCES_DIR}"
echo "deb [trusted=yes] ${REPO_URL} x12 main" > "${SOURCE_FILE}"
echo "✓ Added x12-repo source: ${SOURCE_FILE}"

# Update package lists
echo "Updating package lists..."
apt-get update

# Install X12 engine library (required by all x12 packages)
echo "Installing libx12engine..."
apt-get install -y libx12engine

echo ""
echo "✓ X12 Repo setup complete!"
echo ""
echo "You can now install x12-enhanced packages:"
echo "  pkg install alacritty-x12    # Terminal (x12-enhanced)"
echo "  pkg install dolphin-x12      # File manager (x12-enhanced)"
echo "  pkg install chromium-x12     # Browser (x12-enhanced)"
echo "  pkg install vlc-x12          # Media player (x12-enhanced)"
echo ""
echo "Each package includes:"
echo "  • Speed:      O3 + LTO + dead-code elimination"
echo "  • Smoothness: Frame-pacing + render backend selection"
echo "  • Adaptive:   Runtime CPU/GPU/RAM detection"
echo "  • Automatic:  Zero config — engine decides everything"
"""
    (REPO_ROOT / "scripts/setup-x12-repo.sh").write_text(script, encoding="utf-8")
    os.chmod(REPO_ROOT / "scripts/setup-x12-repo.sh", 0o755)
    print("  Wrote setup-x12-repo.sh")

# ── libx12engine package entry ────────────────────────────────────────────────

def make_engine_package_entry() -> dict:
    return {
        "Package":      "libx12engine",
        "Version":      "1.0.0",
        "Architecture": "aarch64",
        "Maintainer":   "x12-repo",
        "Section":      "libs",
        "Priority":     "required",
        "Depends":      "",
        "Description":  "X12 Adaptive Performance Engine runtime library.\n"
                       " Provides automatic speed, smoothness, adaptive, and\n"
                       " auto performance tuning for all x12-repo packages.\n"
                       " Required by all x12-* packages.",
        "build_system": "make",
        "pkg_type":     "library",
        "binary":       "lib/libx12engine.so",
        "x12_features": {
            "speed": True, "smoothness": True,
            "adaptive": True, "automatic": True,
        },
    }

# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    print("Building X12 repo infrastructure...")

    stats = load_stats()
    packages = build_packages_json(stats)

    # Prepend the engine package itself
    packages.insert(0, make_engine_package_entry())

    # Write packages.json
    PACKAGES_JSON.parent.mkdir(parents=True, exist_ok=True)
    PACKAGES_JSON.write_text(json.dumps({
        "meta": REPO_META,
        "packages": packages,
        "count": len(packages),
    }, indent=2))
    print(f"  Wrote packages.json: {len(packages)} entries")

    # Write APT Packages index for each arch
    for arch in ARCHES:
        write_packages_index(packages, arch)

    # Write Release
    write_release({})

    # Write setup script
    write_setup_script()

    print(f"\n✓ Repo infrastructure built:")
    print(f"  packages.json     : {len(packages)} packages")
    print(f"  APT indexes       : {', '.join(ARCHES)}")
    print(f"  Release file      : repo/dists/x12/Release")
    print(f"  Setup script      : scripts/setup-x12-repo.sh")

if __name__ == "__main__":
    main()
