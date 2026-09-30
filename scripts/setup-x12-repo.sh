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
