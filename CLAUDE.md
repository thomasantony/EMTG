# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**EMTG** (Evolutionary Mission Trajectory Generator) is a NASA Goddard Space Flight Center open-source global optimization tool for preliminary spacecraft mission design. It solves interplanetary trajectory optimization problems using nonlinear programming (NLP) solvers and monotonic basin hopping (MBH).

- **Language:** C++17 (core engine), Python 3 (GUI, post-processing, code generation)
- **License:** NASA Open Source Agreement (NOSA)
- **Build System:** CMake 3.8+
- **Primary Executable:** `EMTGv9`

## Build Commands

### Prerequisites

EMTG requires these dependencies (see `docs/0_Users/build_system/linux_build_system/` for full install instructions):

| Dependency | Purpose | Notes |
|------------|---------|-------|
| CSpice N0067 | Ephemeris lookups | Always required. Download from NAIF. |
| Boost 1.79.0 | filesystem, serialization, system | Always required. |
| GSL 2.7.0 | Cubic splines (SplineEphem) | Required when `SPLINE_EPHEM=ON` (default). Use the AMPL fork for CMake support. |
| IPOPT | NLP solver (open-source) | Required when `USE_IPOPT=ON` (default). Install via `brew install ipopt` or `apt install coinor-libipopt-dev`. |
| SNOPT 7.x | NLP solver (proprietary) | Required when `USE_SNOPT=ON`. Not included in repo. |

### First-Time Setup

`EMTG-Config.cmake` **must exist** before running CMake — it is not in the repository. Create it from the template and set your local dependency paths:

```bash
cp EMTG-Config-template.cmake EMTG-Config.cmake
# Edit EMTG-Config.cmake and set:
#   CSPICE_DIR   — path to CSpice root
#   BOOST_ROOT   — path to Boost root (and BOOST_INCLUDE_DIR, BOOST_LIBRARY_DIRS)
#   GSL_PATH     — path to GSL build directory
#   IPOPT_ROOT_DIR  — only needed if IPOPT is not on the system path
#   SNOPT_ROOT_DIR  — only needed when USE_SNOPT=ON
```

### Building

```bash
cmake -B build                           # IPOPT only (default)
cmake -B build -DUSE_SNOPT=ON            # Both SNOPT and IPOPT
cmake -B build -DUSE_SNOPT=ON -DUSE_IPOPT=OFF  # SNOPT only

cmake --build build -j$(nproc)

# Build output: bin/EMTGv9
```

At least one of `USE_SNOPT` or `USE_IPOPT` must be enabled. Other key options: `SPLINE_EPHEM` (ON), `BACKGROUND_MODE` (ON on Unix), `FAST_EMTG_MATRIX` (ON). See `CMakeLists.txt` for the full list.

## Running

```bash
./bin/EMTGv9 path/to/mission.emtgopt   # Run with specific options file
./bin/EMTGv9                             # Uses default.emtgopt in current directory
```

To use IPOPT as the solver, set `NLP_solver_type 2` in the `.emtgopt` file.

## Testing

Regression tests use a custom Python framework (`testatron/`). Tests run EMTG on `.emtgopt` files and compare outputs against baseline `.emtg` truth files (tolerance: `1.0e-10`).

```bash
cd testatron

# Run all tests
python testatron.py -e /path/to/EMTGv9 -p /path/to/PyEMTG/

# Run specific test folders
python testatron.py -e /path/to/EMTGv9 -f transcription_tests/ solver_options/

# Run a single test case
python testatron.py -e /path/to/EMTGv9 -c tests/transcription_tests/MGALT_EMintercept

# Update truth files after intentional output changes
python testatron.py -e /path/to/EMTGv9 --update_truths
```

Test categories live in `testatron/tests/` (137 tests across 10 folders). Results go to `test_results.csv` and `failed_tests.csv`.

## Critical Warnings

1. **Auto-generated files — do not edit manually:**
   - `src/Core/missionoptions.h/.cpp`, `src/Core/journeyoptions.h/.cpp`
   - `PyEMTG/MissionOptions.py`, `PyEMTG/JourneyOptions.py`
   - Generated from `OptionsOverhaul/list_of_missionoptions.csv` and `list_of_journeyoptions.csv` by `PyEMTG/OptionsOverhaul/make_EMTG_missionoptions_journeyoptions.py`
   - To add options: update the CSV, then re-run the generator

2. **SNOPT is proprietary and now optional** — not included in the repo. `depend/snopt/` has only CMake configs and a placeholder. The CMake auto-detects SNOPT version (7.2/7.5/7.6/7.7) from directory structure. EMTG can now be built with IPOPT only (`-DUSE_SNOPT=OFF -DUSE_IPOPT=ON`).

3. **Conditional compilation** — feature-gated code uses `#ifdef SPLINE_EPHEM`, `#ifdef USE_SNOPT`, `#ifdef USE_IPOPT`, `#ifdef SNOPT72/75/76/77`, `#ifdef HAS_PROBEENTRYPHASE`, `#ifdef HAS_BUILT_IN_THRUSTERS`, `#ifdef BACKGROUND_MODE`

## Architecture

### NLP Formulation

The entire mission is formulated as a single NLP problem with decision variables (X), constraints (F), and Jacobian entries (G). The solver calls `problem::evaluate()` which triggers the full evaluation chain. Supported solvers:
- **SNOPT** (proprietary, `NLP_solver_type 0`) — Sequential Quadratic Programming via `SNOPT_interface`
- **IPOPT** (open-source, `NLP_solver_type 2`) — Interior Point method via `IPOPT_interface` with L-BFGS Hessian approximation

### NLP Solver Architecture

```
NLP_interface (abstract base, src/InnerLoop/NLP_interface.h)
├── SNOPT_interface (owns snoptProblemExtension, guarded by USE_SNOPT)
└── IPOPT_interface (owns EMTG_IPOPT_NLP : Ipopt::TNLP, guarded by USE_IPOPT)
```

MBH and FilamentWalker use `NLP_interface*` polymorphically. The solver is selected in `problem::optimize()` based on `NLP_solver_type`.

### Core Class Hierarchy

```
problem (abstract NLP base, src/Core/problem.h)
  └── Mission (src/Mission/mission.h) — top-level orchestration, owns Journeys
        └── Journey (src/Mission/Journey/journey.h) — sequence of Phases
              └── phase (abstract, src/Mission/Journey/Phase/phase.h)
                    ├── TwoPointShootingPhase → MGAnDSMs, CoastPhase, MGALT, FBLT
                    └── ParallelShootingPhase → PSFBphase, PSBIphase
```

### Hot Path

```
NLP solver → problem::evaluate() → Mission → Journey → Phase
  → Propagator::propagate() → ExplicitRungeKutta::step() [repeated]
    → EOM::evaluate() [4× for RK4, 13× for DP87]
      → SpacecraftAccelerationModel::computeAcceleration()
```

### Key Patterns

- **Factory pattern** throughout: phases, propagators, integrators, state representations, EOMs, objective functions, acceleration model terms, hardware models
- **Options-driven configuration**: `missionoptions` and `journeyoptions` classes control all behavior. `.emtgopt` files are the user-facing interface
- **Modular acceleration model**: `SpacecraftAccelerationModel` aggregates `AccelerationModelTerm` implementations (gravity, thrust, drag, SRP, harmonics)
- **Deep directory nesting**: `src/Mission/Journey/Phase/` has 6+ levels of subdirectories matching the inheritance hierarchy

### Key Source Directories

| Directory | Purpose |
|-----------|---------|
| `src/Executable/` | Entry point (`EMTG_v9.cpp`) |
| `src/Core/` | Options, enums, abstract `problem` base class |
| `src/Mission/` | Mission → Journey → Phase hierarchy, objective functions |
| `src/InnerLoop/` | NLP solver interfaces (SNOPT, IPOPT, WORHP), MBH global search |
| `src/Astrodynamics/` | Orbital mechanics, gravity, atmosphere, state representations, EOMs |
| `src/Integration/` | Runge-Kutta integrators (RK4, DP87) |
| `src/Propagation/` | Kepler and integrated propagators |
| `src/HardwareModels/` | Spacecraft, launch vehicle, propulsion, power models |
| `src/Scalatron/` | NLP variable scaling framework |
| `src/SplineEphem/` | Spline-based ephemeris (requires GSL, gated by `SPLINE_EPHEM`) |
| `PyEMTG/` | wxPython GUI, `.emtg`/`.emtgopt` parsing, post-processing, PEATSA |

### Build Targets

The `src/CMakeLists.txt` creates a static library `emtg` (all source except `Executable/`) and links it into the `EMTGv9` executable. Optional targets: `PropulatorDriver`, `propulator` (Python module), `PyHardware` (Python module).

## Code Style

- Namespace: `EMTG::` with sub-namespaces (`EMTG::HardwareModels::`, `EMTG::Astrodynamics::`, etc.)
- Header guards: `#pragma once`
- File naming: PascalCase for classes, snake_case for utilities
- Copyright header required on all source files (NASA Open Source License)
- Indentation: mixed (4-space and tab depending on file history)
- Units: km, km/s, kg, seconds internally. Epochs use MJD in seconds from J2000

## When Modifying Code

- Check if a file is auto-generated before editing (look for "auto-generated by" comments)
- New phase types: follow the factory pattern and inheritance hierarchy
- New acceleration terms: implement the `AccelerationModelTerm` interface
- New objective functions: follow patterns in `src/Mission/ObjectiveFunctions/`
- New NLP solvers: inherit from `NLP_interface`, add conditional compilation with `#ifdef USE_<SOLVER>`
- Options changes must maintain backward compatibility with existing `.emtgopt` files
- Run testatron regression tests after significant changes
