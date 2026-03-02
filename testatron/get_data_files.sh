#!/usr/bin/env bash
# Download external data files required for EMTG Testatron regression tests.
#
# Files downloaded:
#   universe/ephemeris_files/  - SPICE kernels from NAIF/JPL
#
# Files copied from within the repo:
#   HardwareModels/NLSII_April2017.emtg_launchvehicleopt
#   HardwareModels/NLSII_August2018.emtg_launchvehicleopt
#   (copied from docs/0_Users/tutorial/.../LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt)
#
# Usage:
#   cd /path/to/testatron
#   bash get_data_files.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EPHEMERIS_DIR="${SCRIPT_DIR}/universe/ephemeris_files"
HARDWARE_DIR="${SCRIPT_DIR}/HardwareModels"
PUBLSII="${SCRIPT_DIR}/../docs/0_Users/tutorial/Tutorial_EMTG_Files/Config_Files/hardware_models/LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt"

NAIF_BASE="https://naif.jpl.nasa.gov/pub/naif/generic_kernels"

echo "=============================================="
echo " EMTG Testatron Data File Downloader"
echo "=============================================="
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

copy_lv_file() {
    local dest="${HARDWARE_DIR}/$1"
    if [ -f "${dest}" ]; then
        echo "  [SKIP] $1 already exists"
        return
    fi
    if [ ! -f "${PUBLSII}" ]; then
        echo "  [FAIL] Source not found: ${PUBLSII}"
        echo "         Cannot create $1"
        return 1
    fi
    cp "${PUBLSII}" "${dest}"
    echo "  [COPY] $1"
    echo "         <- $(basename "${PUBLSII}")"
}

copy_lv_file "NLSII_April2017.emtg_launchvehicleopt"
copy_lv_file "NLSII_August2018.emtg_launchvehicleopt"

echo ""
echo "  Source: LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt"
echo "  (publicly distributable NLS-II data included with the EMTG tutorial)"
echo ""
echo "=============================================="
echo " Setup complete."
echo " SPICE kernels:     ${EPHEMERIS_DIR}"
echo " Launch vehicles:   ${HARDWARE_DIR}"
echo "=============================================="
