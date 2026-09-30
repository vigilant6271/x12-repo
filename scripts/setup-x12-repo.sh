#!/data/data/com.termux/files/usr/bin/bash
# X12 Repo Setup Script
# ======================
# Installs the required Termux build tools, clones the project,
# and builds the X12 engine locally on-device.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/vigilant6271/x12-repo/635a1b5/scripts/setup-x12-repo.sh | bash

set -euo pipefail

REPO_OWNER="${REPO_OWNER:-vigilant6271}"
REPO_NAME="${REPO_NAME:-x12-repo}"
REPO_BRANCH="${REPO_BRANCH:-main}"
INSTALL_DIR="${INSTALL_DIR:-$HOME/x12-repo}"
GIT_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}.git"

printf '%s\n' "X12 Repo bootstrap"
printf '%s\n' "=================="

if [ ! -d "${PREFIX}" ]; then
    echo "ERROR: This script must run inside Termux."
    exit 1
fi

pkg update -y
pkg install -y git clang make python python-venv

if [ ! -d "${INSTALL_DIR}/.git" ]; then
    echo "Cloning ${GIT_URL} into ${INSTALL_DIR}..."
    git clone --depth 1 --branch "${REPO_BRANCH}" "${GIT_URL}" "${INSTALL_DIR}"
else
    echo "Repo already present at ${INSTALL_DIR}; updating..."
    git -C "${INSTALL_DIR}" pull --ff-only origin "${REPO_BRANCH}"
fi

cd "${INSTALL_DIR}"
make -C engine clean
make -C engine TARGET_ARCH=aarch64 -j"$(nproc 2>/dev/null || echo 2)"
make -C engine install PREFIX="${PREFIX}"

if [ -x "${PREFIX}/bin/x12-info" ]; then
    echo ""
    "${PREFIX}/bin/x12-info" | head -n 20 || true
fi

echo ""
echo "Setup complete."
echo "Run:"
echo "  cd ${INSTALL_DIR}"
echo "  python3 ci/orchestrator.py --engine-only"
echo ""
echo "This repo is ready to test on Termux."
