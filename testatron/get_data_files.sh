#!/usr/bin/env bash
# Download external data files required for EMTG Testatron regression tests.
#
# Files downloaded:
#   universe/ephemeris_files/  - SPICE kernels from NAIF/JPL
#
# Files NOT downloaded (not publicly available):
#   HardwareModels/NLSII_April2017.emtg_launchvehicleopt
#   HardwareModels/NLSII_August2018.emtg_launchvehicleopt
#   See HardwareModels/go_get_these_files.txt for details.
#
# Usage:
#   cd /path/to/testatron
#   bash get_data_files.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EPHEMERIS_DIR="${SCRIPT_DIR}/universe/ephemeris_files"

NAIF_BASE="https://naif.jpl.nasa.gov/pub/naif/generic_kernels"

echo "=============================================="
echo " EMTG Testatron Data File Downloader"
echo "=============================================="
echo ""
echo "Destination: ${EPHEMERIS_DIR}"
echo ""

mkdir -p "${EPHEMERIS_DIR}"

# Choose download tool
if command -v curl &> /dev/null; then
    DOWNLOAD="curl -L -C - -o"
elif command -v wget &> /dev/null; then
    DOWNLOAD="wget -c -O"
else
    echo "ERROR: Neither curl nor wget found. Please install one and retry."
    exit 1
fi

download_file() {
    local url="$1"
    local dest="$2"
    local description="$3"

    if [ -f "${dest}" ]; then
        echo "  [SKIP] ${description} already exists: $(basename "${dest}")"
        return
    fi

    echo "  [DOWN] ${description}"
    echo "         -> ${url}"
    ${DOWNLOAD} "${dest}" "${url}"
    echo "         Done."
}

echo "--- SPICE Kernel Files ---"

# Leap seconds kernel (~5 KB)
download_file \
    "${NAIF_BASE}/lsk/naif0012.tls" \
    "${EPHEMERIS_DIR}/naif0012.tls" \
    "Leap seconds kernel (naif0012.tls)"

# Planetary constants kernel (~123 KB)
download_file \
    "${NAIF_BASE}/pck/pck00010.tpc" \
    "${EPHEMERIS_DIR}/pck00010.tpc" \
    "Planetary constants kernel (pck00010.tpc)"

# Planetary ephemeris DE430 (~114 MB)
download_file \
    "${NAIF_BASE}/spk/planets/de430.bsp" \
    "${EPHEMERIS_DIR}/de430.bsp" \
    "Planetary ephemeris DE430 (de430.bsp, ~114 MB)"

# Mars satellite kernel (~10 MB)
download_file \
    "${NAIF_BASE}/spk/satellites/mar099.bsp" \
    "${EPHEMERIS_DIR}/mar099.bsp" \
    "Mars satellite kernel (mar099.bsp)"

# Jupiter satellite kernel
download_file \
    "${NAIF_BASE}/spk/satellites/jup365.bsp" \
    "${EPHEMERIS_DIR}/jup365.bsp" \
    "Jupiter satellite kernel (jup365.bsp)"

echo ""
echo "--- Launch Vehicle Library Files ---"
echo ""
echo "  [SKIP] NLSII_April2017.emtg_launchvehicleopt"
echo "  [SKIP] NLSII_August2018.emtg_launchvehicleopt"
echo ""
echo "  These files contain NASA Launch Services Program (NLS-II) data"
echo "  and are not publicly available for automatic download."
echo "  See HardwareModels/go_get_these_files.txt for details."
echo ""
echo "=============================================="
echo " Download complete."
echo " SPICE kernels written to:"
echo "   ${EPHEMERIS_DIR}"
echo "=============================================="
