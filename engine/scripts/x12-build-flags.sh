#!/bin/bash
# x12-build-flags.sh
# ====================
# Sourced by every x12 package build.sh.
# Injects the X12 performance engine into the build system.
#
# Sets:
#   X12_CFLAGS, X12_CXXFLAGS, X12_LDFLAGS  — compiler/linker flags
#   X12_CMAKE_ARGS                          — CMake configure arguments
#   X12_MESON_ARGS                          — Meson configure arguments
#   x12_inject_configure_args()             — call before ./configure
#   x12_inject_cmake()                      — call before cmake
#   x12_inject_meson()                      — call before meson setup
#   x12_post_install()                      — call after make install

# ── Base optimisation flags ────────────────────────────────────────────
# Applied to every package regardless of type.
X12_OPT_CORE="-O3 -flto=thin -ffunction-sections -fdata-sections"
X12_OPT_TUNE="-fomit-frame-pointer -fno-semantic-interposition"
X12_OPT_WARNOFF="-Wno-error=deprecated-declarations -Wno-error=unused-result"

# Architecture-specific SIMD
case "${TERMUX_ARCH:-}" in
  aarch64)
    X12_ARCH_FLAGS="-march=armv8-a+simd+crypto+crc+dotprod -mtune=cortex-a76"
    ;;
  arm)
    X12_ARCH_FLAGS="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=softfp"
    ;;
  x86_64)
    X12_ARCH_FLAGS="-march=x86-64-v2 -msse4.2 -mpopcnt"
    ;;
  i686)
    X12_ARCH_FLAGS="-march=i686 -msse2"
    ;;
  *)
    X12_ARCH_FLAGS=""
    ;;
esac

# LTO + dead-code elimination linker flags
X12_LD_OPT="-Wl,--gc-sections -Wl,--icf=safe -Wl,-O2"
X12_LD_ENGINE="-lx12engine -lpthread"

# Full flag sets
X12_CFLAGS="${X12_OPT_CORE} ${X12_ARCH_FLAGS} ${X12_OPT_TUNE} ${X12_OPT_WARNOFF}"
X12_CXXFLAGS="${X12_CFLAGS} -fvisibility-inlines-hidden"
X12_LDFLAGS="${X12_LD_OPT} ${X12_LD_ENGINE}"

# ── Per-package-type additions ─────────────────────────────────────────
x12_apply_pkg_type_flags() {
    local pkg_type="${1:-generic}"
    case "$pkg_type" in
      game)
        X12_CFLAGS+=" -ffast-math -funroll-loops -fprefetch-loop-arrays"
        X12_CXXFLAGS+=" -ffast-math -funroll-loops -fprefetch-loop-arrays"
        ;;
      media)
        X12_CFLAGS+=" -ffast-math -fvectorize"
        X12_CXXFLAGS+=" -ffast-math -fvectorize"
        X12_LDFLAGS+=" -lavcodec -lavformat"
        ;;
      browser)
        # Browsers have their own LTO; don't double-apply
        X12_OPT_CORE="-O2 -ffunction-sections -fdata-sections"
        X12_CFLAGS="${X12_OPT_CORE} ${X12_ARCH_FLAGS} ${X12_OPT_WARNOFF}"
        X12_CXXFLAGS="${X12_CFLAGS}"
        ;;
      wm|terminal)
        X12_CFLAGS+=" -fstack-protector-strong"
        X12_CXXFLAGS+=" -fstack-protector-strong"
        ;;
      library)
        # Shared libs: no LTO across DSO boundaries (use lto-thin only)
        # Already set above
        ;;
    esac
}

# ── CMake arguments ────────────────────────────────────────────────────
X12_CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
    "-DCMAKE_C_FLAGS_RELEASE=${X12_CFLAGS}"
    "-DCMAKE_CXX_FLAGS_RELEASE=${X12_CXXFLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=${X12_LDFLAGS}"
    "-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=${X12_LDFLAGS}"
    "-DBUILD_TESTING=OFF"
    "-DBUILD_DOCS=OFF"
    "-DBUILD_EXAMPLES=OFF"
    "-DENABLE_TESTS=OFF"
    "-DX12_ENGINE=ON"
)

# ── Meson arguments ────────────────────────────────────────────────────
X12_MESON_ARGS=(
    "--buildtype=release"
    "--optimization=3"
    "-Db_lto=true"
    "-Db_lto_mode=thin"
    "-Db_ndebug=true"
    "-Db_pie=true"
)

# ── Autotools configure args ───────────────────────────────────────────
X12_CONFIGURE_ARGS=(
    "--enable-optimizations"
    "--disable-debug"
    "--disable-tests"
    "--disable-docs"
)

# ── Injection helpers ──────────────────────────────────────────────────

# Call before ./configure
x12_inject_configure_args() {
    export CFLAGS="${CFLAGS:-} ${X12_CFLAGS}"
    export CXXFLAGS="${CXXFLAGS:-} ${X12_CXXFLAGS}"
    export LDFLAGS="${LDFLAGS:-} ${X12_LDFLAGS}"
}

# Call before cmake
x12_inject_cmake() {
    # Appends X12 args to CMake invocation
    # Usage: cmake "${X12_CMAKE_ARGS[@]}" <other args> ..
    echo "X12: injecting CMake performance flags"
}

# Call before meson setup
x12_inject_meson() {
    export CFLAGS="${CFLAGS:-} ${X12_CFLAGS}"
    export CXXFLAGS="${CXXFLAGS:-} ${X12_CXXFLAGS}"
    export LDFLAGS="${LDFLAGS:-} ${X12_LDFLAGS}"
}

# ── Post-install: wrap the binary + set rpath ─────────────────────────
# $1 = binary path relative to $TERMUX_PREFIX
# $2 = package type (gui|game|media|browser|ide|wm|terminal|tool)
x12_post_install() {
    local bin_rel="${1:-}"
    local pkg_type="${2:-generic}"
    local prefix="${TERMUX_PREFIX:-/data/data/com.termux/files/usr}"
    local bin_path="${prefix}/${bin_rel}"

    [ -z "$bin_rel" ] && return 0
    [ ! -f "$bin_path" ] && return 0

    # Rename real binary to <name>.real
    local bin_dir
    bin_dir=$(dirname "$bin_path")
    local bin_name
    bin_name=$(basename "$bin_path")

    mv "${bin_path}" "${bin_path}.real"

    # Write wrapper script
    cat > "${bin_path}" <<WRAPPER
#!/data/data/com.termux/files/usr/bin/bash
exec "${prefix}/bin/x12-wrap" "${pkg_type}" "${bin_path}.real" "\$@"
WRAPPER
    chmod 755 "${bin_path}"
    echo "X12: wrapped ${bin_name} as ${pkg_type}"
}

# ── Version bump helper ────────────────────────────────────────────────
# Appends +x12 suffix to TERMUX_PKG_VERSION
x12_bump_version() {
    if [[ "${TERMUX_PKG_VERSION:-}" != *"+x12"* ]]; then
        TERMUX_PKG_VERSION="${TERMUX_PKG_VERSION}+x12"
    fi
}
