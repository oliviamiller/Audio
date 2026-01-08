#!/bin/bash
# Bundle JACK libraries with the module for deployment
set -euo pipefail

# Only needed on Linux
[[ "$(uname -s)" != "Linux" ]] && exit 0

BUILD_DIR="${1:-build/install}"
LIB_DIR="${BUILD_DIR}/lib"
mkdir -p "${LIB_DIR}"

echo "Bundling JACK libraries..."

# Find and copy JACK library files (actual files and symlinks)
found=0
while IFS= read -r -d '' lib; do
    echo "  Copying: $lib"
    # Copy preserving symlinks
    cp -a "$lib" "${LIB_DIR}/"
    found=1
done < <(find /usr/lib /lib -name "libjack.so*" \( -type f -o -type l \) -print0 2>/dev/null)

if [ "$found" -eq 0 ]; then
    echo "  No JACK libraries found (this is OK if JACK is not installed)"
fi

echo "Done. Libraries in ${LIB_DIR}:"
ls -lh "${LIB_DIR}" 2>/dev/null || echo "  (no libraries bundled)"

