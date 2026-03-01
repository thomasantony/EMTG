# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**EMTG** (Evolutionary Mission Trajectory Generator) is a NASA Goddard Space Flight Center open-source global optimization tool for preliminary spacecraft mission design. It solves interplanetary trajectory optimization problems using nonlinear programming (NLP) solvers and monotonic basin hopping (MBH).

- **Language:** C++17 (core engine), Python 3 (GUI, post-processing, code generation)
- **License:** NASA Open Source Agreement (NOSA)
- **Build System:** CMake 3.8+
- **Primary Executable:** `EMTGv9`

## Build Commands

```bash
# 1. Configure dependency paths (one-time setup)
cp EMTG-Config-template.cmake EMTG-Config.cmake
# Edit EMTG-Config.cmake to set: CSPICE_DIR, SNOPT_ROOT_DIR, BOOST_ROOT, GSL_PATH

# 2. Generate and build
cmake -B build
cmake --build build

# Build output: bin/EMTGv9
```

Default build type is Release. Key CMake options: `SPLINE_EPHEM` (ON), `BACKGROUND_MODE` (ON on Unix), `SAFE_SNOPT` (ON), `FAST_EMTG_MATRIX` (ON). See `CMakeLists.txt` for full list.

## Running

```bash
./bin/EMTGv9 path/to/mission.emtgopt   # Run with specific options file
./bin/EMTGv9                             # Uses default.emtgopt in current directory
```

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

2. **SNOPT is proprietary** — not included in the repo. `depend/snopt/` has only CMake configs and a placeholder. The CMake auto-detects SNOPT version (7.2/7.5/7.6/7.7) from directory structure.

3. **Conditional compilation** — feature-gated code uses `#ifdef SPLINE_EPHEM`, `#ifdef SNOPT72/75/76/77`, `#ifdef HAS_PROBEENTRYPHASE`, `#ifdef HAS_BUILT_IN_THRUSTERS`, `#ifdef BACKGROUND_MODE`

## Architecture

### NLP Formulation

The entire mission is formulated as a single NLP problem with decision variables (X), constraints (F), and Jacobian entries (G). The solver (SNOPT or WORHP) calls into `SNOPT_interface::SNOPT_user_function()` which triggers the full evaluation chain.

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
SNOPT_user_function() → problem::evaluate() → Mission → Journey → Phase
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
| `src/InnerLoop/` | NLP solver interfaces (SNOPT, WORHP), MBH global search |
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
- Options changes must maintain backward compatibility with existing `.emtgopt` files
- Run testatron regression tests after significant changes
