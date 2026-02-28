# CLAUDE.md — EMTG Development Guide

## Project Overview

**EMTG** (Evolutionary Mission Trajectory Generator) is a NASA Goddard Space Flight Center open-source global optimization tool for preliminary spacecraft mission design. It solves complex interplanetary trajectory optimization problems using nonlinear programming (NLP) solvers and monotonic basin hopping (MBH).

- **Version:** 9.02
- **Language:** C++17 (core engine), Python 3 (GUI, post-processing, code generation)
- **License:** NASA Open Source Agreement (NOSA)
- **Build System:** CMake 3.8+
- **Primary Executable:** `EMTGv9`

## Repository Structure

```
EMTG/
├── src/                    # C++ source code (599 files, ~136K lines)
│   ├── Executable/         # Main entry point (EMTG_v9.cpp)
│   ├── Core/               # Mission/journey options, enums, problem base class
│   ├── Mission/            # Mission, Journey, Phase hierarchy
│   │   ├── Journey/Phase/  # Phase types (see "Phase Types" below)
│   │   └── ObjectiveFunctions/  # 46 objective function implementations
│   ├── Astrodynamics/      # Orbital mechanics, gravity, atmosphere models
│   │   ├── AccelerationModel/   # Thrust, gravity, drag, SRP terms
│   │   ├── StateRepresentation/ # Cartesian, COE, MEE, B-plane, etc.
│   │   └── EquationsOfMotion/   # Time-domain and Sundman EOMs
│   ├── InnerLoop/          # NLP solver interfaces (SNOPT, WORHP), MBH
│   ├── Integration/        # Runge-Kutta integrators (RK4, DP87)
│   ├── Propagation/        # Kepler and integrated propagators
│   ├── HardwareModels/     # Spacecraft, launch vehicle, propulsion, power
│   ├── Math/               # Matrix, tensor, interpolation, math utilities
│   ├── Scalatron/          # NLP scaling framework
│   ├── SplineEphem/        # Spline-based ephemeris (optional, requires GSL)
│   ├── Propulator/         # Python bindings for propulsion modeling
│   ├── Python/             # PyHardware Python bindings
│   ├── Utilities/          # File, string, time utilities
│   ├── Testing/            # C++ test infrastructure
│   └── lib/                # Library support files
├── PyEMTG/                 # Python GUI and post-processing (127 files)
│   ├── Mission.py          # Parse .emtg output files
│   ├── MissionOptions.py   # Auto-generated mission options (from C++ headers)
│   ├── JourneyOptions.py   # Auto-generated journey options (from C++ headers)
│   ├── StateConverter.py   # Orbital state representation conversions
│   ├── PyEMTG_interface.py # wxPython GUI main frame
│   ├── PEATSA/             # Parametric Exploration And Trajectory Search
│   ├── HighFidelity/       # High-fidelity trajectory analysis
│   ├── ReportGenerators/   # Post-processing report tools
│   ├── Converters/         # Options file version converters
│   ├── OptionsOverhaul/    # Code generation for options classes
│   ├── JourneyOptionsPanel/  # GUI panels for journey configuration
│   ├── SpacecraftOptionsPanel/  # GUI panels for spacecraft config
│   ├── SpiceyPy_Utilities/ # SPICE ephemeris wrappers
│   ├── SimpleMonteCarlo/   # Monte Carlo uncertainty analysis
│   └── ConvergenceAnimator/  # Optimization convergence visualization
├── testatron/              # Regression test system (Python)
│   ├── testatron.py        # Test runner script
│   ├── tests/              # 137 test cases in 10 categories
│   ├── universe/           # Test ephemeris and universe data
│   └── HardwareModels/     # Test hardware configurations
├── docs/                   # Documentation
│   ├── 0_Users/            # User guides, tutorials, build instructions
│   └── 1_Developers/       # Math specs, software design, Python API docs
├── HardwareModels/         # Default hardware configuration files
├── Universe/               # Celestial body definitions (.emtg_universe files)
├── OptionsOverhaul/        # Options definition CSVs
├── depend/                 # Bundled dependencies (Boost, CSpice, GSL, MinGW)
├── bin/                    # Build output directory
├── CMakeLists.txt          # Root CMake configuration
├── EMTG-Config-template.cmake  # Dependency paths template
├── Doxyfile                # Doxygen configuration
└── README.opensource       # Project readme
```

## Building EMTG

### Prerequisites

1. CMake 3.8+
2. C++17-compatible compiler (GCC, MSVC)
3. SNOPT 7.6 (commercial, not bundled — must be acquired separately)
4. Bundled in `depend/`: Boost 1.79.0, CSpice 64-bit, GSL 2.4.0, MinGW 7.2.0 (Windows)

### Build Steps

```bash
# 1. Configure dependency paths
cp EMTG-Config-template.cmake EMTG-Config.cmake
# Edit EMTG-Config.cmake to set: CSPICE_DIR, SNOPT_ROOT_DIR, BOOST_ROOT, GSL_PATH

# 2. Generate build system
cmake -B build

# 3. Build
cmake --build build
```

The built `EMTGv9` executable is placed in `bin/`.

### Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `SPLINE_EPHEM` | ON | Enable SplineEphem (requires GSL) |
| `BUILD_EMTG_TESTBED` | OFF | Build C++ testing framework |
| `BUILD_PROPULATOR` | OFF | Build Propulator Python module |
| `BUILD_PYHARDWARE` | OFF | Build PyHardware Python module |
| `QUIET_SNOPT` | OFF | Minimize SNOPT console output |
| `SAFE_SNOPT` | ON | Trap conditions that could crash SNOPT |
| `FAST_EMTG_MATRIX` | ON | Remove bounds checking from matrix class |
| `HAS_PROBEENTRYPHASE` | ON | Build with ProbeEntryPhase support |
| `HAS_BUILT_IN_THRUSTERS` | ON | Include built-in thruster library |
| `BACKGROUND_MODE` | ON (Unix) | Suppress "press any key" prompts |

## Running EMTG

```bash
# Run with a mission options file
./bin/EMTGv9 path/to/mission.emtgopt

# Run with default.emtgopt in current directory
./bin/EMTGv9
```

Results are written to `EMTG_v9_results/` (or a configured working directory).

### Key File Formats

| Extension | Description |
|-----------|-------------|
| `.emtgopt` | Mission options input file |
| `.emtg` | Mission output/solution file |
| `.emtg_universe` | Celestial body universe definition |
| `.emtg_spacecraftopt` | Spacecraft configuration |
| `.emtg_launchvehicleopt` | Launch vehicle configuration |
| `.emtg_propulsionsystemopt` | Propulsion system configuration |
| `.emtg_powersystemsopt` | Power system configuration |
| `.ThrottleTable` | Electric propulsion throttle table |

## Testing

### Testatron (Regression Tests)

The test system is a custom Python framework in `testatron/`. It runs EMTG on `.emtgopt` files and compares outputs against baseline `.emtg` truth files.

```bash
cd testatron

# Run all tests
python testatron.py -e /path/to/EMTGv9 -p /path/to/PyEMTG/

# Run specific test folders
python testatron.py -e /path/to/EMTGv9 -f transcription_tests/ solver_options/

# Run specific test cases
python testatron.py -e /path/to/EMTGv9 -c tests/transcription_tests/MGALT_EMintercept

# Run only mission tests
python testatron.py -e /path/to/EMTGv9 -m

# Update truth files after intentional changes
python testatron.py -e /path/to/EMTGv9 --update_truths
```

**Test categories** (in `testatron/tests/`):
- `global_mission_options/` — 28 tests
- `journey_options/` — 33 tests
- `output_options/` — 5 tests
- `physics_options/` — 10 tests
- `script_constraint_tests/` — 13 tests
- `solver_options/` — 5 tests
- `spacecraft_options/` — 30 tests
- `state_representation_tests/` — 6 tests
- `transcription_tests/` — 7 tests
- `mission_tests/` — placeholder for user tests

The test runner generates `test_results.csv` and `failed_tests.csv` summary reports. Default comparison tolerance: `1.0e-10`.

## Architecture

### Core Design Patterns

- **Factory Pattern**: Used extensively for phases, propagators, integrators, state representations, EOMs, objective functions, acceleration model terms, and hardware
- **Inheritance Hierarchy**: `problem` (abstract) → `Mission` → `Journey` → `phase` → specific phase types
- **Options Pattern**: Centralized configuration via `missionoptions` / `journeyoptions` classes
- **NLP Formulation**: The entire mission is formulated as a nonlinear programming problem with decision variables (X), constraints (F), and Jacobian entries (G)

### Phase Types

Trajectory phases (in `src/Mission/Journey/Phase/`):

**TwoPointShootingPhase:**
- `MGAnDSMs` — Multiple Gravity Assist with n Deep Space Maneuvers (patched conics)
- `CoastPhase` — Unpowered coasting
  - `SundmanCoastPhase` — Sundman time-transformed coast
  - `ControlLawThrustPhase` — Control law guidance thrust
- `MGALT` — Multiple Gravity Assist Low Thrust
- `FBLT` — Full Boundary-value Low Thrust

**ParallelShootingPhase:**
- `PSFBphase` — Parallel Shooting Full Boundary
- `PSBIphase` — Parallel Shooting Bounded Impulse

**Other:**
- `ProbeEntryPhase` — Atmospheric probe entry

### Acceleration Model

The `SpacecraftAccelerationModel` aggregates modular acceleration terms:
- `CentralForceTerm` — Central body gravity
- `SphericalHarmonicTerm` — Gravity harmonics
- `ThrustTerm` — Propulsive thrust
- `AerodynamicDragTerm` — Atmospheric drag
- `SolarRadiationPressureTerm` — SRP
- `GravityTerm` — Third-body gravity

### State Representations

Multiple coordinate systems supported (in `src/Astrodynamics/StateRepresentation/`):
- Cartesian (position/velocity)
- COE (Classical Orbital Elements)
- MEE (Modified Equinoctial Elements)
- SphericalAZFPA, SphericalRADEC
- IncomingAsymptote, OutgoingAsymptote
- B-plane parameters

### Optimization Solvers

- **SNOPT** (primary) — Sparse Nonlinear Optimizer, versions 7.2–7.7 supported
- **WORHP** (alternative) — NLP solver
- **MBH** — Monotonic Basin Hopping global search wrapper
- **NSGAII** — Multi-objective evolutionary algorithm (outer loop)

### Key C++ Classes

| Class | File | Purpose |
|-------|------|---------|
| `missionoptions` | `src/Core/missionoptions.h` | All mission-level configuration |
| `journeyoptions` | `src/Core/journeyoptions.h` | Per-journey configuration |
| `problem` | `src/Core/problem.h` | Abstract NLP problem base |
| `Mission` | `src/Mission/mission.h` | Top-level mission orchestration |
| `phase` | `src/Mission/Journey/Phase/phase.h` | Abstract trajectory phase |
| `universe` | `src/Astrodynamics/universe.h` | Celestial system definition |
| `body` | `src/Astrodynamics/body.h` | Celestial body properties |
| `Spacecraft` | `src/HardwareModels/Spacecraft.h` | Spacecraft model |
| `SNOPT_interface` | `src/InnerLoop/SNOPT_interface.h` | SNOPT solver wrapper |
| `snoptProblemExtension` | `src/InnerLoop/snoptProblemExtension.h` | SNOPT problem adapter (modify for SNOPT version changes) |

## Code Generation

The `missionoptions` and `journeyoptions` C++ classes and their Python equivalents are **auto-generated** from CSV definitions:

- **Input CSVs:** `OptionsOverhaul/list_of_missionoptions.csv`, `OptionsOverhaul/list_of_journeyoptions.csv`
- **Generator script:** `PyEMTG/OptionsOverhaul/make_EMTG_missionoptions_journeyoptions.py`
- **C++ outputs:** `src/Core/missionoptions.h/.cpp`, `src/Core/journeyoptions.h/.cpp`
- **Python outputs:** `PyEMTG/MissionOptions.py`, `PyEMTG/JourneyOptions.py`

When adding new mission/journey options, update the CSV and re-run the generator. Do not manually edit the auto-generated files.

## PyEMTG

### Overview

PyEMTG is a wxPython GUI and post-processing toolkit. It creates `.emtgopt` files, launches the C++ `EMTGv9` executable via subprocess, and parses `.emtg` output files for analysis and visualization.

### Dependencies

- wxPython
- NumPy, SciPy
- Matplotlib
- Astropy
- SpiceyPy (optional, for SPICE ephemeris)

### Configuration

Edit `PyEMTG/PyEMTG.options` to set:
- `EMTG_path` — Path to the EMTGv9 executable
- `default_universe_path` — Path to universe data files
- `default_thruster_file` — Default throttle table

### Key Modules

| Module | Purpose |
|--------|---------|
| `Mission.py` | Parse `.emtg` output files into Python objects |
| `MissionOptions.py` | Read/write `.emtgopt` files (auto-generated) |
| `JourneyOptions.py` | Journey-level options (auto-generated) |
| `StateConverter.py` | Convert between 6 orbital state representations |
| `PEATSA/` | Parametric trade study automation framework |
| `HighFidelity/` | High-fidelity trajectory reconstruction |
| `ReportGenerators/` | Maneuver tables, throttle reports, distance analysis |
| `auto_boost_python.py` | Generates Boost.Python bindings for hardware models |

## Conventions for AI Assistants

### Code Style

- C++ source uses C++17 features
- Namespace: `EMTG::` for all core classes, with sub-namespaces like `EMTG::HardwareModels::`, `EMTG::Astrodynamics::`
- Header guards: `#pragma once`
- File naming: PascalCase for classes (e.g., `SpacecraftAccelerationModel.h`), snake_case for utilities (e.g., `file_utilities.h`)
- Copyright header required on all source files (NASA Open Source License)
- Indentation: mixed (4-space and tab depending on file history)

### Important Warnings

1. **Do not manually edit auto-generated files**: `missionoptions.h/.cpp`, `journeyoptions.h/.cpp`, `MissionOptions.py`, `JourneyOptions.py` are generated from CSV definitions
2. **SNOPT is proprietary**: It is not included in the repository. The `depend/snopt/` directory contains only CMake configs and a placeholder file
3. **Options files are the primary interface**: The `.emtgopt` file format is how users configure missions. Changes to options must maintain backward compatibility with existing `.emtgopt` files
4. **Conditional compilation**: Use `#ifdef SPLINE_EPHEM`, `#ifdef SNOPT72/75/76/77` for feature-gated code
5. **The Mission/Journey/Phase hierarchy is deep**: Navigate carefully through `src/Mission/Journey/Phase/` — there are 6+ levels of subdirectories

### When Modifying Code

- Read the relevant source files before making changes
- Check if the file is auto-generated (look for comments like "auto-generated by make_EMTG_missionoptions_journeyoptions.py")
- Maintain the factory pattern when adding new phase types, propagators, or state representations
- Ensure new acceleration model terms follow the `AccelerationModelTerm` interface
- New objective functions should follow the pattern in `src/Mission/ObjectiveFunctions/`
- Run the testatron regression tests after significant changes
- Units: EMTG uses km, km/s, kg, seconds internally. Epochs use Modified Julian Date (MJD) in seconds from J2000
