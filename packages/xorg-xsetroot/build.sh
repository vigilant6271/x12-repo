TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="xorg-xsetroot — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${TERMUX_PKG_VERSION}+x12"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${TERMUX_PKG_BUILDER_DIR}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "generic"

termux_step_configure() {
    x12_inject_configure_args
    ./configure \
        --prefix="${TERMUX_PREFIX}" \
        --host="${TERMUX_HOST_PLATFORM}" \
        --disable-debug \
        --disable-tests \
        --disable-docs \
        --enable-optimizations \
        "${TERMUX_PKG_EXTRA_CONFIGURE_ARGS:-}"
}

termux_step_make() {
    make -j"${TERMUX_MAKE_PROCESSES}"
}

termux_step_make_install() {
    make install
    x12_post_install "bin/xorg-xsetroot" "generic"
    x12_bump_version
}
