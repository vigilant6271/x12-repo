TERMUX_PKG_HOMEPAGE="https://github.com/termux/termux-packages"
TERMUX_PKG_DESCRIPTION="korganizer — X12-enhanced build"
TERMUX_PKG_LICENSE="custom"
TERMUX_PKG_MAINTAINER="x12-repo"
TERMUX_PKG_VERSION="${TERMUX_PKG_VERSION}+x12"
TERMUX_PKG_SRCURL="https://github.com/termux/termux-packages/tree/master/x11-packages/korganizer"
TERMUX_PKG_DEPENDS="libx12engine"

# ── Load X12 build flags ──────────────────────────────────────────────
source "${TERMUX_PKG_BUILDER_DIR}/../../engine/scripts/x12-build-flags.sh"
x12_apply_pkg_type_flags "generic"

termux_step_configure() {
    termux_setup_cmake
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
        -DCMAKE_C_FLAGS="${X12_CFLAGS}" \
        -DCMAKE_CXX_FLAGS="${X12_CXXFLAGS}" \
        -DCMAKE_EXE_LINKER_FLAGS="${X12_LDFLAGS}" \
        -DCMAKE_SHARED_LINKER_FLAGS="${X12_LDFLAGS}" \
        -DBUILD_TESTING=OFF \
        -DBUILD_DOCS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DENABLE_TESTS=OFF \
        -DX12_ENGINE=ON \
        "${TERMUX_PKG_SRCDIR}"
}

termux_step_make() {
    make -j"${TERMUX_MAKE_PROCESSES}"
}

termux_step_make_install() {
    make install
    x12_post_install "bin/korganizer" "generic"
    x12_bump_version
}
