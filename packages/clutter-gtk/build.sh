TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="clutter-gtk — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${TERMUX_PKG_VERSION}+x12"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${TERMUX_PKG_BUILDER_DIR}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "gui"

termux_step_configure() {
    x12_inject_meson
    meson setup \
        --buildtype=release \
        --optimization=3 \
        -Db_lto=true \
        -Db_lto_mode=thin \
        -Db_ndebug=true \
        -Db_pie=true \
        "${TERMUX_PKG_SRCDIR}" .
}

termux_step_make() {
    ninja -j"${TERMUX_MAKE_PROCESSES}"
}

termux_step_make_install() {
    ninja install
    x12_post_install "bin/clutter-gtk" "gui"
    x12_bump_version
}
