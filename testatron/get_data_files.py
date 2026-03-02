"""
Set up external data files required for EMTG Testatron regression tests.

Files downloaded from NAIF/JPL:
    universe/ephemeris_files/  - SPICE kernels

Files copied from within the repo:
    HardwareModels/NLSII_April2017.emtg_launchvehicleopt
    HardwareModels/NLSII_August2018.emtg_launchvehicleopt
    (copied from docs/0_Users/tutorial/.../LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt)

Usage:
    cd /path/to/testatron
    python get_data_files.py
"""

import os
import shutil
import sys
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EPHEMERIS_DIR = os.path.join(SCRIPT_DIR, 'universe', 'ephemeris_files')
HARDWARE_DIR = os.path.join(SCRIPT_DIR, 'HardwareModels')
PUBLSII = os.path.join(
    SCRIPT_DIR, '..', 'docs', '0_Users', 'tutorial',
    'Tutorial_EMTG_Files', 'Config_Files', 'hardware_models',
    'LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt'
)

NAIF_BASE = 'https://naif.jpl.nasa.gov/pub/naif/generic_kernels'

SPICE_FILES = [
    {
        'url': f'{NAIF_BASE}/lsk/naif0012.tls',
        'filename': 'naif0012.tls',
        'description': 'Leap seconds kernel (~5 KB)',
    },
    {
        'url': f'{NAIF_BASE}/pck/pck00010.tpc',
        'filename': 'pck00010.tpc',
        'description': 'Planetary constants kernel (~123 KB)',
    },
    {
        'url': f'{NAIF_BASE}/spk/planets/de430.bsp',
        'filename': 'de430.bsp',
        'description': 'Planetary ephemeris DE430 (~114 MB)',
    },
    {
        'url': f'{NAIF_BASE}/spk/satellites/mar099.bsp',
        'filename': 'mar099.bsp',
        'description': 'Mars satellite kernel',
    },
    {
        'url': f'{NAIF_BASE}/spk/satellites/jup365.bsp',
        'filename': 'jup365.bsp',
        'description': 'Jupiter satellite kernel',
    },
]


def _progress_hook(block_count, block_size, total_size):
    """Print a simple download progress indicator."""
    downloaded = block_count * block_size
    if total_size > 0:
        pct = min(100.0, downloaded / total_size * 100)
        bar_len = 40
        filled = int(bar_len * pct / 100)
        bar = '#' * filled + '-' * (bar_len - filled)
        mb_done = downloaded / 1024 / 1024
        mb_total = total_size / 1024 / 1024
        print(f'\r         [{bar}] {pct:5.1f}%  {mb_done:.1f}/{mb_total:.1f} MB',
              end='', flush=True)
    else:
        mb = downloaded / 1024 / 1024
        print(f'\r         {mb:.1f} MB downloaded...', end='', flush=True)


def download_file(url, dest_path, description):
    """Download a file if it does not already exist."""
    filename = os.path.basename(dest_path)
    if os.path.exists(dest_path):
        print(f'  [SKIP] {description} already exists: {filename}')
        return True

    print(f'  [DOWN] {description}')
    print(f'         {url}')
    try:
        urllib.request.urlretrieve(url, dest_path, _progress_hook)
        print()  # newline after progress bar
        print(f'         Done.')
        return True
    except Exception as exc:
        print()
        print(f'  [FAIL] Could not download {filename}: {exc}')
        # Remove partial file if it exists
        if os.path.exists(dest_path):
            os.remove(dest_path)
        return False


def main():
    print('==============================================')
    print(' EMTG Testatron Data File Downloader')
    print('==============================================')
    print()
    print(f'Destination: {EPHEMERIS_DIR}')
    print()

    os.makedirs(EPHEMERIS_DIR, exist_ok=True)

    print('--- SPICE Kernel Files ---')
    print()

    failures = []
    for entry in SPICE_FILES:
        dest = os.path.join(EPHEMERIS_DIR, entry['filename'])
        ok = download_file(entry['url'], dest, entry['description'])
        if not ok:
            failures.append(entry['filename'])
        print()

    print('--- Launch Vehicle Library Files ---')
    print()

    src = os.path.normpath(PUBLSII)
    lv_names = [
        'NLSII_April2017.emtg_launchvehicleopt',
        'NLSII_August2018.emtg_launchvehicleopt',
    ]
    if not os.path.exists(src):
        print(f'  [FAIL] Source file not found:')
        print(f'         {src}')
        failures.extend(lv_names)
    else:
        for name in lv_names:
            dest = os.path.join(HARDWARE_DIR, name)
            if os.path.exists(dest):
                print(f'  [SKIP] {name} already exists')
            else:
                shutil.copy2(src, dest)
                print(f'  [COPY] {name}')
                print(f'         <- {os.path.basename(src)}')
        print()
        print(f'  Source: LaunchVehicles_PubliclyDistributable_NLSII.emtg_launchvehicleopt')
        print(f'  (publicly distributable NLS-II data included with the EMTG tutorial)')

    print()
    print('==============================================')

    if failures:
        print(' WARNING: The following files could not be set up:')
        for f in failures:
            print(f'   - {f}')
        print()
        sys.exit(1)
    else:
        print(' Setup complete.')
        print(f' SPICE kernels:   {EPHEMERIS_DIR}')
        print(f' Launch vehicles: {HARDWARE_DIR}')
        print('==============================================')


if __name__ == '__main__':
    main()
