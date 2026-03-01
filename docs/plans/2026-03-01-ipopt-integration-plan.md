# IPOPT Integration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add IPOPT as an open-source alternative NLP solver to EMTG, fully compatible with MBH, FilamentWalker, and all existing testatron tests.

**Architecture:** New `IPOPT_interface` (inherits `NLP_interface`) with internal `EMTG_IPOPT_NLP` adapter (inherits `Ipopt::TNLP`). MBH and FilamentWalker refactored from `SNOPT_interface*` to `NLP_interface*`. Solver selection via `NLP_solver_type` option (2=IPOPT). F/G arrays split from SNOPT's combined format to IPOPT's separated objective/constraint format.

**Tech Stack:** C++17, IPOPT (Coin-OR), CMake 3.8+, Python 3 (code generation)

**Design doc:** `docs/plans/2026-03-01-ipopt-integration-design.md`

---

## Task 1: CMake Infrastructure

**Files:**
- Create: `cmake/FindIPOPT.cmake`
- Modify: `CMakeLists.txt` (add USE_IPOPT option near line 170, IPOPT detection after SNOPT section ~line 312)
- Modify: `EMTG-Config-template.cmake` (add IPOPT_ROOT_DIR)

**Step 1: Create `cmake/FindIPOPT.cmake`**

```cmake
# FindIPOPT.cmake - Locate IPOPT library
# Sets: IPOPT_FOUND, IPOPT_INCLUDE_DIRS, IPOPT_LIBRARIES, IPOPT_DEFINITIONS
#
# User can set IPOPT_ROOT_DIR to guide search

include(FindPackageHandleStandardArgs)

# Try pkg-config first (standard on Linux)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(IPOPT_PKG QUIET ipopt)
endif()

if(IPOPT_PKG_FOUND)
    set(IPOPT_INCLUDE_DIRS ${IPOPT_PKG_INCLUDE_DIRS})
    set(IPOPT_LIBRARIES ${IPOPT_PKG_LIBRARIES})
    set(IPOPT_DEFINITIONS ${IPOPT_PKG_CFLAGS_OTHER})
    set(IPOPT_LIBRARY_DIRS ${IPOPT_PKG_LIBRARY_DIRS})
else()
    # Fall back to manual search using IPOPT_ROOT_DIR
    find_path(IPOPT_INCLUDE_DIR
        NAMES IpIpoptApplication.hpp
        PATH_SUFFIXES coin coin-or coin/ThirdParty include/coin include/coin-or
        HINTS ${IPOPT_ROOT_DIR} ENV IPOPT_ROOT_DIR
        PATHS /usr/local /usr /opt/local /opt)

    find_library(IPOPT_LIBRARY
        NAMES ipopt libipopt ipopt-3 ipopt-0
        PATH_SUFFIXES lib lib64
        HINTS ${IPOPT_ROOT_DIR} ENV IPOPT_ROOT_DIR
        PATHS /usr/local /usr /opt/local /opt)

    set(IPOPT_INCLUDE_DIRS ${IPOPT_INCLUDE_DIR})
    set(IPOPT_LIBRARIES ${IPOPT_LIBRARY})
endif()

find_package_handle_standard_args(IPOPT
    REQUIRED_VARS IPOPT_LIBRARIES IPOPT_INCLUDE_DIRS)

mark_as_advanced(IPOPT_INCLUDE_DIRS IPOPT_LIBRARIES IPOPT_DEFINITIONS)
```

**Step 2: Add USE_IPOPT option to `CMakeLists.txt`**

After the existing `option()` block (around line 37), add:

```cmake
option(USE_IPOPT "Build with IPOPT solver support" OFF)
```

After the SNOPT section (after line ~312, before the `set(EMTG_LIBRARIES ...)` line), add:

```cmake
#---------------------------------------------------------------------
# IPOPT configuration
#---------------------------------------------------------------------
if(USE_IPOPT)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    find_package(IPOPT REQUIRED)
    message(STATUS "IPOPT found: ${IPOPT_LIBRARIES}")
    message(STATUS "IPOPT includes: ${IPOPT_INCLUDE_DIRS}")
    include_directories(${IPOPT_INCLUDE_DIRS})
    add_definitions(-DUSE_IPOPT)
    list(APPEND EMTG_LIBRARIES ${IPOPT_LIBRARIES})
    if(IPOPT_LIBRARY_DIRS)
        link_directories(${IPOPT_LIBRARY_DIRS})
    endif()
endif()
```

**Step 3: Add IPOPT_ROOT_DIR to `EMTG-Config-template.cmake`**

After the SNOPT_ROOT_DIR section, add:

```cmake
# IPOPT (optional, open-source alternative to SNOPT)
# Download from https://github.com/coin-or/Ipopt
# Or install via package manager: apt-get install coinor-libipopt-dev
#set(IPOPT_ROOT_DIR "/path/to/ipopt")
```

**Step 4: Commit**

```bash
git add cmake/FindIPOPT.cmake CMakeLists.txt EMTG-Config-template.cmake
git commit -m "feat: add CMake infrastructure for IPOPT support"
```

---

## Task 2: Update Options CSV and Regenerate Code

**Files:**
- Modify: `OptionsOverhaul/list_of_missionoptions.csv` (line 12: NLP_solver_type; add new rows after line 31)
- Run: `PyEMTG/OptionsOverhaul/make_EMTG_missionoptions_journeyoptions.py`
- Auto-generated outputs: `src/Core/missionoptions.h`, `src/Core/missionoptions.cpp`, `PyEMTG/MissionOptions.py`

**Step 1: Update `NLP_solver_type` in CSV (line 12)**

Change:
```csv
int,NLP_solver_type,,,0,0,1,"NLP solver type. Choices are 0 - SNOPT, 1 - WORHP",NLP solver type\n#0: SNOPT\n#1: WORHP
```
To:
```csv
int,NLP_solver_type,,,0,0,2,"NLP solver type. Choices are 0 - SNOPT, 1 - WORHP, 2 - IPOPT",NLP solver type\n#0: SNOPT\n#1: WORHP\n#2: IPOPT
```

**Step 2: Add IPOPT-specific options after line 31 (after `snopt_max_run_time`)**

Insert these rows:
```csv
int,ipopt_max_iterations,,,-1,-1,inf,IPOPT max iterations (-1 uses snopt_major_iterations),IPOPT max iterations (-1 uses snopt_major_iterations)
double,ipopt_convergence_tolerance,,,-1,-1,1,IPOPT convergence tolerance (-1 uses snopt_optimality_tolerance),IPOPT convergence tolerance (-1 uses snopt_optimality_tolerance)
double,ipopt_constraint_violation_tolerance,,,-1,-1,1,IPOPT constraint violation tolerance (-1 uses snopt_feasibility_tolerance),IPOPT constraint violation tolerance (-1 uses snopt_feasibility_tolerance)
int,ipopt_max_run_time,,,-1,-1,inf,IPOPT max CPU time in seconds (-1 uses snopt_max_run_time),IPOPT max CPU time in seconds (-1 uses snopt_max_run_time)
int,ipopt_mu_strategy,,,1,0,1,"IPOPT barrier parameter strategy. Choices are 0 - monotone, 1 - adaptive",IPOPT barrier parameter strategy\n#0: monotone\n#1: adaptive
int,ipopt_print_level,,,-1,-1,12,IPOPT print level 0-12 (-1 auto from quiet_NLP),IPOPT print level 0-12 (-1 auto from quiet_NLP)
```

**Step 3: Run the code generator**

```bash
cd /workspace
python PyEMTG/OptionsOverhaul/make_EMTG_missionoptions_journeyoptions.py
```

Expected: Regenerated `src/Core/missionoptions.h`, `src/Core/missionoptions.cpp`, `PyEMTG/MissionOptions.py`, `PyEMTG/JourneyOptions.py` with the new IPOPT option fields.

**Step 4: Verify the generated code has the new fields**

Search for `ipopt_max_iterations` in `src/Core/missionoptions.h` — should appear as a member variable declaration.

**Step 5: Commit**

```bash
git add OptionsOverhaul/list_of_missionoptions.csv src/Core/missionoptions.h src/Core/missionoptions.cpp PyEMTG/MissionOptions.py PyEMTG/JourneyOptions.py
git commit -m "feat: add IPOPT-specific options to mission options CSV and regenerate code"
```

---

## Task 3: Extend NLPoptions for IPOPT

**Files:**
- Modify: `src/InnerLoop/NLPoptions.h` (add IPOPT fields after line 98)
- Modify: `src/InnerLoop/NLPoptions.cpp` (add IPOPT initialization + missionoptions mapping)

**Step 1: Add IPOPT fields to `NLPoptions.h`**

After line 79 (before the `protected:` section), add getters/setters:
```cpp
            // IPOPT-specific options
            int get_ipopt_max_iterations() { return this->ipopt_max_iterations; }
            double get_ipopt_convergence_tolerance() { return this->ipopt_convergence_tolerance; }
            double get_ipopt_constraint_violation_tolerance() { return this->ipopt_constraint_violation_tolerance; }
            int get_ipopt_max_run_time() { return this->ipopt_max_run_time; }
            int get_ipopt_mu_strategy() { return this->ipopt_mu_strategy; }
            int get_ipopt_print_level() { return this->ipopt_print_level; }

            void set_ipopt_max_iterations(const int& v) { this->ipopt_max_iterations = v; }
            void set_ipopt_convergence_tolerance(const double& v) { this->ipopt_convergence_tolerance = v; }
            void set_ipopt_constraint_violation_tolerance(const double& v) { this->ipopt_constraint_violation_tolerance = v; }
            void set_ipopt_max_run_time(const int& v) { this->ipopt_max_run_time = v; }
            void set_ipopt_mu_strategy(const int& v) { this->ipopt_mu_strategy = v; }
            void set_ipopt_print_level(const int& v) { this->ipopt_print_level = v; }
```

After line 101 (in the `protected` section, after `output_file_path`), add:
```cpp
            // IPOPT-specific fields
            int ipopt_max_iterations;
            double ipopt_convergence_tolerance;
            double ipopt_constraint_violation_tolerance;
            int ipopt_max_run_time;
            int ipopt_mu_strategy;
            int ipopt_print_level;
```

**Step 2: Initialize IPOPT defaults in `NLPoptions.cpp`**

In the default constructor (after line 42, before the closing brace), add:
```cpp
            ipopt_max_iterations(-1),
            ipopt_convergence_tolerance(-1.0),
            ipopt_constraint_violation_tolerance(-1.0),
            ipopt_max_run_time(-1),
            ipopt_mu_strategy(1),
            ipopt_print_level(-1)
```

In the `missionoptions` constructor (after line 64, before the `#endif`), add:
```cpp
            this->ipopt_max_iterations = options.ipopt_max_iterations;
            this->ipopt_convergence_tolerance = options.ipopt_convergence_tolerance;
            this->ipopt_constraint_violation_tolerance = options.ipopt_constraint_violation_tolerance;
            this->ipopt_max_run_time = options.ipopt_max_run_time;
            this->ipopt_mu_strategy = options.ipopt_mu_strategy;
            this->ipopt_print_level = options.ipopt_print_level;
```

**Step 3: Commit**

```bash
git add src/InnerLoop/NLPoptions.h src/InnerLoop/NLPoptions.cpp
git commit -m "feat: extend NLPoptions with IPOPT-specific configuration fields"
```

---

## Task 4: Refactor MBH to Use NLP_interface

**Files:**
- Modify: `src/InnerLoop/monotonic_basin_hopping.h` (lines 28, 42-43, 49-50, 122)
- Modify: `src/InnerLoop/monotonic_basin_hopping.cpp` (constructor, initialize, hop, time_hop, slide methods)

**Step 1: Update header `monotonic_basin_hopping.h`**

Change line 27 from:
```cpp
#include "SNOPT_interface.h"
```
To:
```cpp
#include "NLP_interface.h"
```

Change constructor (lines 42-43) from:
```cpp
            MBH(EMTG::problem* myProblem,
                SNOPT_interface* mySNOPT);
```
To:
```cpp
            MBH(EMTG::problem* myProblem,
                NLP_interface* myNLP);
```

Change initialize() (lines 49-50) from:
```cpp
            void initialize(EMTG::problem* Problem_input,
                SNOPT_interface* mySNOPT);
```
To:
```cpp
            void initialize(EMTG::problem* Problem_input,
                NLP_interface* myNLP);
```

Change member field (line 122) from:
```cpp
            SNOPT_interface* mySNOPT;
```
To:
```cpp
            NLP_interface* myNLP;
```

**Step 2: Update implementation `monotonic_basin_hopping.cpp`**

Replace all occurrences of `mySNOPT` with `myNLP` throughout the file. Key locations:
- Constructor: parameter name `mySNOPT` → `myNLP`, assignment `this->mySNOPT = mySNOPT` → `this->myNLP = myNLP`
- `initialize()`: same changes
- `hop()` and `time_hop()`: all calls like `this->mySNOPT->run_NLP()`, `this->mySNOPT->getX_unscaled()`, `this->mySNOPT->setX0_unscaled()`, `this->mySNOPT->setJGlobalIncumbent()`, etc.
- `slide()`: same
- Remove the WORHP deprecation check (lines 320-325) — the solver type is now determined by which NLP_interface* was passed in

Also remove `#include "SNOPT_interface.h"` if present in the .cpp file (it should get NLP_interface via the header).

**Step 3: Verify the refactor compiles**

If possible, build to verify. Otherwise, review that all `mySNOPT->` calls use methods defined on `NLP_interface` (run_NLP, getX_unscaled, setX0_unscaled, getF, setJGlobalIncumbent, getJGlobalIncumbent, getX_NLP_incumbent_unscaled, getF_NLP_incumbent, getfeasibility_metric, getfeasibility_metric_NLP_incumbent).

**Step 4: Commit**

```bash
git add src/InnerLoop/monotonic_basin_hopping.h src/InnerLoop/monotonic_basin_hopping.cpp
git commit -m "refactor: MBH accepts NLP_interface instead of SNOPT_interface

Enables MBH to work with any NLP solver, not just SNOPT.
No behavioral change - same SNOPT_interface object flows through."
```

---

## Task 5: Refactor FilamentWalker to Use NLP_interface

**Files:**
- Modify: `src/InnerLoop/FilamentWalker.h` (lines 27, 44-45, 65)
- Modify: `src/InnerLoop/FilamentWalker.cpp` (constructor, all mySNOPT references)

**Step 1: Update header `FilamentWalker.h`**

Change line 27 from:
```cpp
#include "SNOPT_interface.h"
```
To:
```cpp
#include "NLP_interface.h"
```

Change constructor (lines 44-45) from:
```cpp
            FilamentWalker(problem* myProblem,
                           SNOPT_interface* mySNOPT);
```
To:
```cpp
            FilamentWalker(problem* myProblem,
                           NLP_interface* myNLP);
```

Change member field (line 65) from:
```cpp
            SNOPT_interface* mySNOPT;
```
To:
```cpp
            NLP_interface* myNLP;
```

**Step 2: Update implementation `FilamentWalker.cpp`**

Replace all occurrences of `mySNOPT` with `myNLP` throughout the file.

**Step 3: Commit**

```bash
git add src/InnerLoop/FilamentWalker.h src/InnerLoop/FilamentWalker.cpp
git commit -m "refactor: FilamentWalker accepts NLP_interface instead of SNOPT_interface"
```

---

## Task 6: Create EMTG_IPOPT_NLP Adapter Class

**Files:**
- Create: `src/InnerLoop/EMTG_IPOPT_NLP.h`
- Create: `src/InnerLoop/EMTG_IPOPT_NLP.cpp`

This is the internal adapter that inherits from `Ipopt::TNLP` and translates EMTG's combined F/G format into IPOPT's separated objective/constraint format.

**Step 1: Create `src/InnerLoop/EMTG_IPOPT_NLP.h`**

```cpp
// EMTG: Evolutionary Mission Trajectory Generator
// An open-source global optimization tool for preliminary mission design
// Provided by NASA Goddard Space Flight Center
//
// Copyright (c) 2013 - 2024 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration.
// All Other Rights Reserved.

// Licensed under the NASA Open Source License (the "License");
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
// https://opensource.org/licenses/NASA-1.3
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied.   See the License for the specific language
// governing permissions and limitations under the License.

// IPOPT TNLP adapter for EMTG
// Translates EMTG's combined F/G format into IPOPT's separated
// objective/constraint/Jacobian format.

#pragma once

#ifdef USE_IPOPT

#include "IpTNLP.hpp"
#include "problem.h"
#include "NLPoptions.h"
#include "NLP_interface.h"

#include <vector>
#include <ctime>

namespace EMTG
{
    namespace Solvers
    {
        // Forward declaration
        class IPOPT_interface;

        class EMTG_IPOPT_NLP : public Ipopt::TNLP
        {
        public:
            EMTG_IPOPT_NLP(IPOPT_interface* myIPOPT_interface,
                           problem* myProblem,
                           const NLPoptions& myOptions);

            virtual ~EMTG_IPOPT_NLP() {}

            // TNLP required methods
            virtual bool get_nlp_info(Ipopt::Index& n,
                                      Ipopt::Index& m,
                                      Ipopt::Index& nnz_jac_g,
                                      Ipopt::Index& nnz_h_lag,
                                      IndexStyleEnum& index_style) override;

            virtual bool get_bounds_info(Ipopt::Index n,
                                         Ipopt::Number* x_l,
                                         Ipopt::Number* x_u,
                                         Ipopt::Index m,
                                         Ipopt::Number* g_l,
                                         Ipopt::Number* g_u) override;

            virtual bool get_starting_point(Ipopt::Index n,
                                            bool init_x, Ipopt::Number* x,
                                            bool init_z, Ipopt::Number* z_L, Ipopt::Number* z_U,
                                            Ipopt::Index m,
                                            bool init_lambda, Ipopt::Number* lambda) override;

            virtual bool eval_f(Ipopt::Index n,
                                const Ipopt::Number* x,
                                bool new_x,
                                Ipopt::Number& obj_value) override;

            virtual bool eval_grad_f(Ipopt::Index n,
                                     const Ipopt::Number* x,
                                     bool new_x,
                                     Ipopt::Number* grad_f) override;

            virtual bool eval_g(Ipopt::Index n,
                                const Ipopt::Number* x,
                                bool new_x,
                                Ipopt::Index m,
                                Ipopt::Number* g) override;

            virtual bool eval_jac_g(Ipopt::Index n,
                                    const Ipopt::Number* x,
                                    bool new_x,
                                    Ipopt::Index m,
                                    Ipopt::Index nele_jac,
                                    Ipopt::Index* iRow,
                                    Ipopt::Index* jCol,
                                    Ipopt::Number* values) override;

            virtual bool eval_h(Ipopt::Index n,
                                const Ipopt::Number* x,
                                bool new_x,
                                Ipopt::Number obj_factor,
                                Ipopt::Index m,
                                const Ipopt::Number* lambda,
                                bool new_lambda,
                                Ipopt::Index nele_hess,
                                Ipopt::Index* iRow,
                                Ipopt::Index* jCol,
                                Ipopt::Number* values) override;

            virtual void finalize_solution(Ipopt::SolverReturn status,
                                           Ipopt::Index n,
                                           const Ipopt::Number* x,
                                           const Ipopt::Number* z_L,
                                           const Ipopt::Number* z_U,
                                           Ipopt::Index m,
                                           const Ipopt::Number* g,
                                           const Ipopt::Number* lambda,
                                           Ipopt::Number obj_value,
                                           const Ipopt::IpoptData* ip_data,
                                           Ipopt::IpoptCalculatedQuantities* ip_cq) override;

            virtual bool intermediate_callback(Ipopt::AlgorithmMode mode,
                                               Ipopt::Index iter,
                                               Ipopt::Number obj_value,
                                               Ipopt::Number inf_pr,
                                               Ipopt::Number inf_du,
                                               Ipopt::Number mu,
                                               Ipopt::Number d_norm,
                                               Ipopt::Number regularization_size,
                                               Ipopt::Number alpha_du,
                                               Ipopt::Number alpha_pr,
                                               Ipopt::Index ls_trials,
                                               const Ipopt::IpoptData* ip_data,
                                               Ipopt::IpoptCalculatedQuantities* ip_cq) override;

        private:
            // Evaluate the EMTG problem (with caching via new_x)
            void evaluate_if_new(const Ipopt::Number* x, bool new_x, bool needG);

            // Pointers back to EMTG objects
            IPOPT_interface* myIPOPT_interface;
            problem* myProblem;
            NLPoptions myOptions;

            // Problem dimensions
            size_t nX;  // number of decision variables
            size_t nF;  // total F entries (objective + constraints)
            size_t nG;  // total G entries
            size_t nA;  // total A (linear Jacobian) entries
            Ipopt::Index m;  // number of constraints (nF - 1)

            // Pre-computed index partitioning
            // Objective gradient: G and A entries where iGfun/iAfun == 0
            std::vector<size_t> obj_G_indices;  // indices into G where iGfun[k] == 0
            std::vector<size_t> obj_G_jvar;     // corresponding jGvar values
            std::vector<size_t> obj_A_indices;  // indices into A where iAfun[k] == 0
            std::vector<size_t> obj_A_jvar;     // corresponding jAvar values

            // Constraint Jacobian: G and A entries where iGfun/iAfun > 0
            std::vector<size_t> con_G_indices;    // indices into G where iGfun[k] > 0
            std::vector<Ipopt::Index> con_G_iRow; // shifted row indices (iGfun[k] - 1)
            std::vector<Ipopt::Index> con_G_jCol; // column indices (jGvar[k])
            std::vector<size_t> con_A_indices;    // indices into A where iAfun[k] > 0
            std::vector<Ipopt::Index> con_A_iRow; // shifted row indices (iAfun[k] - 1)
            std::vector<Ipopt::Index> con_A_jCol; // column indices (jAvar[k])

            Ipopt::Index nnz_jac;  // total nonzeros in constraint Jacobian

            // Cached evaluation results
            std::vector<doubleType> cached_X;
            std::vector<doubleType> cached_F;
            std::vector<double> cached_G;
            bool evaluation_valid;

            // Scaled bounds (from NLP_interface)
            std::vector<double> Xlowerbounds;
            std::vector<double> Xupperbounds;
            std::vector<double> Flowerbounds;
            std::vector<double> Fupperbounds;
            std::vector<double> X_scale_factors;

            // Timer for chaperone
            time_t NLP_start_time;
        };
    } // namespace Solvers
} // namespace EMTG

#endif // USE_IPOPT
```

**Step 2: Create `src/InnerLoop/EMTG_IPOPT_NLP.cpp`**

```cpp
// EMTG: Evolutionary Mission Trajectory Generator
// An open-source global optimization tool for preliminary mission design
// Provided by NASA Goddard Space Flight Center
//
// Copyright (c) 2013 - 2024 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration.
// All Other Rights Reserved.

// Licensed under the NASA Open Source License (the "License");
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
// https://opensource.org/licenses/NASA-1.3
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied.   See the License for the specific language
// governing permissions and limitations under the License.

// IPOPT TNLP adapter implementation

#ifdef USE_IPOPT

#include "EMTG_IPOPT_NLP.h"
#include "IPOPT_interface.h"
#include "EMTG_math.h"

#include <iostream>
#include <cstring>

namespace EMTG
{
    namespace Solvers
    {
        EMTG_IPOPT_NLP::EMTG_IPOPT_NLP(IPOPT_interface* myIPOPT_interface,
                                         problem* myProblem,
                                         const NLPoptions& myOptions)
            : myIPOPT_interface(myIPOPT_interface),
              myProblem(myProblem),
              myOptions(myOptions),
              evaluation_valid(false)
        {
            // Get dimensions from the NLP_interface (which already handles
            // FilamentFinder mode transformations)
            this->nX = myIPOPT_interface->getnX();
            this->nF = myIPOPT_interface->getnF();
            this->nG = myIPOPT_interface->getnG();
            this->m = static_cast<Ipopt::Index>(this->nF - 1);

            // Get sparsity pattern from the NLP_interface
            std::vector<size_t> iGfun = myIPOPT_interface->getiGfun();
            std::vector<size_t> jGvar = myIPOPT_interface->getjGvar();

            // Partition G entries into objective vs constraint
            for (size_t k = 0; k < iGfun.size(); ++k)
            {
                if (iGfun[k] == 0)
                {
                    // Objective gradient entry
                    this->obj_G_indices.push_back(k);
                    this->obj_G_jvar.push_back(jGvar[k]);
                }
                else
                {
                    // Constraint Jacobian entry
                    this->con_G_indices.push_back(k);
                    this->con_G_iRow.push_back(static_cast<Ipopt::Index>(iGfun[k] - 1));
                    this->con_G_jCol.push_back(static_cast<Ipopt::Index>(jGvar[k]));
                }
            }

            // Handle linear entries (A matrix) similarly
            // A entries are constant and set during construction
            // We need to incorporate them into the Jacobian structure
            this->nA = myProblem->iAfun.size();
            for (size_t k = 0; k < this->nA; ++k)
            {
                if (myProblem->iAfun[k] == 0)
                {
                    this->obj_A_indices.push_back(k);
                    this->obj_A_jvar.push_back(myProblem->jAvar[k]);
                }
                else
                {
                    this->con_A_indices.push_back(k);
                    this->con_A_iRow.push_back(static_cast<Ipopt::Index>(myProblem->iAfun[k] - 1));
                    this->con_A_jCol.push_back(static_cast<Ipopt::Index>(myProblem->jAvar[k]));
                }
            }

            this->nnz_jac = static_cast<Ipopt::Index>(this->con_G_indices.size() + this->con_A_indices.size());

            // Copy bounds from NLP_interface (already scaled if Scalatron active)
            this->Xlowerbounds = myIPOPT_interface->getXlowerbounds();
            this->Xupperbounds = myIPOPT_interface->getXupperbounds();
            this->Flowerbounds.assign(myIPOPT_interface->getFlowerbounds().begin() + 1,
                                       myIPOPT_interface->getFlowerbounds().end());
            this->Fupperbounds.assign(myIPOPT_interface->getFupperbounds().begin() + 1,
                                       myIPOPT_interface->getFupperbounds().end());
            this->X_scale_factors = myProblem->X_scale_factors;

            // Allocate cache
            this->cached_X.resize(this->nX);
            this->cached_F.resize(this->nF);
            this->cached_G.resize(iGfun.size());

            this->NLP_start_time = time(NULL);
        }

        bool EMTG_IPOPT_NLP::get_nlp_info(Ipopt::Index& n,
                                            Ipopt::Index& m,
                                            Ipopt::Index& nnz_jac_g,
                                            Ipopt::Index& nnz_h_lag,
                                            IndexStyleEnum& index_style)
        {
            n = static_cast<Ipopt::Index>(this->nX);
            m = this->m;
            nnz_jac_g = this->nnz_jac;
            nnz_h_lag = 0;  // L-BFGS mode, no Hessian
            index_style = C_STYLE;  // 0-based indexing
            return true;
        }

        bool EMTG_IPOPT_NLP::get_bounds_info(Ipopt::Index n,
                                               Ipopt::Number* x_l,
                                               Ipopt::Number* x_u,
                                               Ipopt::Index m,
                                               Ipopt::Number* g_l,
                                               Ipopt::Number* g_u)
        {
            // Variable bounds (scaled to [0, 1] by NLP_interface)
            for (Ipopt::Index i = 0; i < n; ++i)
            {
                x_l[i] = this->Xlowerbounds[i];
                x_u[i] = this->Xupperbounds[i];
            }

            // Constraint bounds (from F[1..nF-1])
            for (Ipopt::Index i = 0; i < m; ++i)
            {
                g_l[i] = this->Flowerbounds[i];
                g_u[i] = this->Fupperbounds[i];
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::get_starting_point(Ipopt::Index n,
                                                  bool init_x, Ipopt::Number* x,
                                                  bool init_z, Ipopt::Number* z_L, Ipopt::Number* z_U,
                                                  Ipopt::Index m,
                                                  bool init_lambda, Ipopt::Number* lambda)
        {
            if (init_x)
            {
                std::vector<doubleType> X0 = myIPOPT_interface->getX0_scaled();
                for (Ipopt::Index i = 0; i < n; ++i)
                    x[i] = X0[i] _GETVALUE;
            }

            // We don't provide initial multipliers
            if (init_z)
            {
                for (Ipopt::Index i = 0; i < n; ++i)
                {
                    z_L[i] = 0.0;
                    z_U[i] = 0.0;
                }
            }

            if (init_lambda)
            {
                for (Ipopt::Index i = 0; i < m; ++i)
                    lambda[i] = 0.0;
            }

            return true;
        }

        void EMTG_IPOPT_NLP::evaluate_if_new(const Ipopt::Number* x,
                                               bool new_x,
                                               bool needG)
        {
            if (new_x || !this->evaluation_valid)
            {
                // Unscale x: x_unscaled = x_scaled * scale_factor + lower_bound
                std::vector<doubleType> X_unscaled(this->nX);
                for (size_t i = 0; i < this->nX; ++i)
                    X_unscaled[i] = x[i] * this->myProblem->X_scale_factors[i]
                                    + this->myProblem->Xlowerbounds[i];

                // Evaluate the problem
                this->myProblem->evaluate(X_unscaled, this->cached_F, this->cached_G, needG);

                // Cache the scaled X for later use
                for (size_t i = 0; i < this->nX; ++i)
                    this->cached_X[i] = x[i];

                this->evaluation_valid = true;
            }
        }

        bool EMTG_IPOPT_NLP::eval_f(Ipopt::Index n,
                                      const Ipopt::Number* x,
                                      bool new_x,
                                      Ipopt::Number& obj_value)
        {
            this->evaluate_if_new(x, new_x, false);

            // F[0] is the objective
            obj_value = this->cached_F[0] _GETVALUE;

            // Handle FeasiblePoint mode: objective = 0
            if (this->myOptions.get_SolverMode() == NLPMode::FeasiblePoint)
                obj_value = 0.0;

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_grad_f(Ipopt::Index n,
                                           const Ipopt::Number* x,
                                           bool new_x,
                                           Ipopt::Number* grad_f)
        {
            this->evaluate_if_new(x, new_x, true);

            // Initialize gradient to zero
            std::memset(grad_f, 0, n * sizeof(Ipopt::Number));

            // Handle FeasiblePoint mode: gradient = 0
            if (this->myOptions.get_SolverMode() == NLPMode::FeasiblePoint)
                return true;

            // Fill from nonlinear G entries where iGfun == 0
            for (size_t k = 0; k < this->obj_G_indices.size(); ++k)
            {
                size_t G_idx = this->obj_G_indices[k];
                size_t j = this->obj_G_jvar[k];
                // Scale the derivative: dF/dx_scaled = dF/dx_unscaled * scale_factor
                grad_f[j] += this->cached_G[G_idx] * this->myProblem->X_scale_factors[j];
            }

            // Fill from linear A entries where iAfun == 0
            for (size_t k = 0; k < this->obj_A_indices.size(); ++k)
            {
                size_t A_idx = this->obj_A_indices[k];
                size_t j = this->obj_A_jvar[k];
                grad_f[j] += this->myProblem->A[A_idx] * this->myProblem->X_scale_factors[j];
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_g(Ipopt::Index n,
                                      const Ipopt::Number* x,
                                      bool new_x,
                                      Ipopt::Index m,
                                      Ipopt::Number* g)
        {
            this->evaluate_if_new(x, new_x, false);

            // Constraints are F[1..nF-1]
            for (Ipopt::Index i = 0; i < m; ++i)
                g[i] = this->cached_F[i + 1] _GETVALUE;

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_jac_g(Ipopt::Index n,
                                          const Ipopt::Number* x,
                                          bool new_x,
                                          Ipopt::Index m,
                                          Ipopt::Index nele_jac,
                                          Ipopt::Index* iRow,
                                          Ipopt::Index* jCol,
                                          Ipopt::Number* values)
        {
            if (values == NULL)
            {
                // Structure call: fill in sparsity pattern
                Ipopt::Index idx = 0;

                // Nonlinear G entries (constraint rows)
                for (size_t k = 0; k < this->con_G_indices.size(); ++k)
                {
                    iRow[idx] = this->con_G_iRow[k];
                    jCol[idx] = this->con_G_jCol[k];
                    ++idx;
                }

                // Linear A entries (constraint rows)
                for (size_t k = 0; k < this->con_A_indices.size(); ++k)
                {
                    iRow[idx] = this->con_A_iRow[k];
                    jCol[idx] = this->con_A_jCol[k];
                    ++idx;
                }
            }
            else
            {
                // Values call: fill in Jacobian values
                this->evaluate_if_new(x, new_x, true);

                Ipopt::Index idx = 0;

                // Nonlinear G entries, scaled
                for (size_t k = 0; k < this->con_G_indices.size(); ++k)
                {
                    size_t G_idx = this->con_G_indices[k];
                    size_t j = this->con_G_jCol[k];
                    // Scale: dF/dx_scaled = dF/dx_unscaled * X_scale_factor
                    values[idx] = this->cached_G[G_idx] * this->myProblem->X_scale_factors[j];
                    ++idx;
                }

                // Linear A entries, scaled (constant)
                for (size_t k = 0; k < this->con_A_indices.size(); ++k)
                {
                    size_t A_idx = this->con_A_indices[k];
                    size_t j = this->con_A_jCol[k];
                    values[idx] = this->myProblem->A[A_idx] * this->myProblem->X_scale_factors[j];
                    ++idx;
                }
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_h(Ipopt::Index n,
                                      const Ipopt::Number* x,
                                      bool new_x,
                                      Ipopt::Number obj_factor,
                                      Ipopt::Index m,
                                      const Ipopt::Number* lambda,
                                      bool new_lambda,
                                      Ipopt::Index nele_hess,
                                      Ipopt::Index* iRow,
                                      Ipopt::Index* jCol,
                                      Ipopt::Number* values)
        {
            // Using L-BFGS approximation — no Hessian needed
            return false;
        }

        bool EMTG_IPOPT_NLP::intermediate_callback(Ipopt::AlgorithmMode mode,
                                                     Ipopt::Index iter,
                                                     Ipopt::Number obj_value,
                                                     Ipopt::Number inf_pr,
                                                     Ipopt::Number inf_du,
                                                     Ipopt::Number mu,
                                                     Ipopt::Number d_norm,
                                                     Ipopt::Number regularization_size,
                                                     Ipopt::Number alpha_du,
                                                     Ipopt::Number alpha_pr,
                                                     Ipopt::Index ls_trials,
                                                     const Ipopt::IpoptData* ip_data,
                                                     Ipopt::IpoptCalculatedQuantities* ip_cq)
        {
            // NLP chaperone: track best feasible point
            if (this->myOptions.get_enable_NLP_chaperone() && this->evaluation_valid)
            {
                double feas_tol = this->myOptions.get_feasibility_tolerance();

                if (inf_pr <= feas_tol)
                {
                    // Current point is feasible
                    doubleType current_J = this->cached_F[0];
                    doubleType incumbent_J = this->myIPOPT_interface->getJ_NLP_incumbent();

                    if (current_J < incumbent_J)
                    {
                        // Update incumbent
                        this->myIPOPT_interface->update_NLP_incumbent(
                            this->cached_X, this->cached_F, this->cached_G, inf_pr);
                    }
                }
            }

            // Check time limit
            time_t now = time(NULL);
            if (difftime(now, this->NLP_start_time) > this->myOptions.get_max_run_time_seconds())
            {
                std::cout << "IPOPT time limit reached." << std::endl;
                return false;  // Request stop
            }

            // Check goal attainment
            if (this->myOptions.get_stop_on_goal_attain())
            {
                if (obj_value <= this->myOptions.get_objective_goal()
                    && inf_pr <= this->myOptions.get_feasibility_tolerance())
                {
                    std::cout << "IPOPT goal attained." << std::endl;
                    return false;  // Request stop
                }
            }

            return true;  // Continue optimization
        }

        void EMTG_IPOPT_NLP::finalize_solution(Ipopt::SolverReturn status,
                                                 Ipopt::Index n,
                                                 const Ipopt::Number* x,
                                                 const Ipopt::Number* z_L,
                                                 const Ipopt::Number* z_U,
                                                 Ipopt::Index m,
                                                 const Ipopt::Number* g,
                                                 const Ipopt::Number* lambda,
                                                 Ipopt::Number obj_value,
                                                 const Ipopt::IpoptData* ip_data,
                                                 Ipopt::IpoptCalculatedQuantities* ip_cq)
        {
            // Copy final scaled solution back to IPOPT_interface
            std::vector<doubleType> X_final_scaled(n);
            for (Ipopt::Index i = 0; i < n; ++i)
                X_final_scaled[i] = x[i];

            this->myIPOPT_interface->set_final_solution(X_final_scaled, status);
        }

    } // namespace Solvers
} // namespace EMTG

#endif // USE_IPOPT
```

**Step 3: Commit**

```bash
git add src/InnerLoop/EMTG_IPOPT_NLP.h src/InnerLoop/EMTG_IPOPT_NLP.cpp
git commit -m "feat: add EMTG_IPOPT_NLP adapter class (Ipopt::TNLP implementation)"
```

---

## Task 7: Create IPOPT_interface Class

**Files:**
- Create: `src/InnerLoop/IPOPT_interface.h`
- Create: `src/InnerLoop/IPOPT_interface.cpp`

**Step 1: Create `src/InnerLoop/IPOPT_interface.h`**

```cpp
// EMTG: Evolutionary Mission Trajectory Generator
// An open-source global optimization tool for preliminary mission design
// Provided by NASA Goddard Space Flight Center
//
// Copyright (c) 2013 - 2024 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration.
// All Other Rights Reserved.

// Licensed under the NASA Open Source License (the "License");
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
// https://opensource.org/licenses/NASA-1.3
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied.   See the License for the specific language
// governing permissions and limitations under the License.

// IPOPT solver interface for EMTG

#pragma once

#ifdef USE_IPOPT

#include "NLP_interface.h"
#include "IpIpoptApplication.hpp"
#include "IpSolveStatistics.hpp"

namespace EMTG
{
    namespace Solvers
    {
        class IPOPT_interface : public NLP_interface
        {
        public:
            IPOPT_interface() : NLP_interface::NLP_interface() {}
            IPOPT_interface(problem* myProblem,
                            const NLPoptions& myOptions);

            virtual void run_NLP(const bool& X0_is_scaled = true) override;

            // Methods called by EMTG_IPOPT_NLP adapter
            std::vector<doubleType> getX0_scaled() const { return this->X0_scaled; }
            std::vector<double> getXlowerbounds() const { return this->Xlowerbounds; }
            std::vector<double> getXupperbounds() const { return this->Xupperbounds; }

            doubleType getJ_NLP_incumbent() const { return this->J_NLP_incumbent; }
            void update_NLP_incumbent(const std::vector<doubleType>& X_scaled,
                                       const std::vector<doubleType>& F,
                                       const std::vector<double>& G,
                                       double feasibility);
            void set_final_solution(const std::vector<doubleType>& X_scaled,
                                     Ipopt::SolverReturn status);

        private:
            Ipopt::ApplicationReturnStatus ipopt_status;
        };
    } // namespace Solvers
} // namespace EMTG

#endif // USE_IPOPT
```

**Step 2: Create `src/InnerLoop/IPOPT_interface.cpp`**

```cpp
// EMTG: Evolutionary Mission Trajectory Generator
// An open-source global optimization tool for preliminary mission design
// Provided by NASA Goddard Space Flight Center
//
// Copyright (c) 2013 - 2024 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration.
// All Other Rights Reserved.

// Licensed under the NASA Open Source License (the "License");
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
// https://opensource.org/licenses/NASA-1.3
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied.   See the License for the specific language
// governing permissions and limitations under the License.

// IPOPT solver interface implementation

#ifdef USE_IPOPT

#include "IPOPT_interface.h"
#include "EMTG_IPOPT_NLP.h"
#include "EMTG_math.h"

#include <iostream>

namespace EMTG
{
    namespace Solvers
    {
        IPOPT_interface::IPOPT_interface(problem* myProblem,
                                          const NLPoptions& myOptions)
            : NLP_interface(myProblem, myOptions),
              ipopt_status(Ipopt::Internal_Error)
        {
        }

        void IPOPT_interface::run_NLP(const bool& X0_is_scaled)
        {
            // Scale/unscale initial guess as needed
            if (X0_is_scaled)
                this->unscaleX0();
            else
                this->scaleX0();

            // Evaluate initial guess
            try
            {
                this->myProblem->evaluate(this->X0_unscaled, this->F, this->G, true);
            }
            catch (std::exception& error)
            {
                std::cout << "IPOPT: Failed to evaluate initial guess: " << error.what() << std::endl;
                return;
            }

            // Initialize NLP chaperone incumbent
            this->J_NLP_incumbent = EMTG::math::LARGE;
            this->feasibility_metric_NLP_incumbent = EMTG::math::LARGE;
            this->first_feasibility = true;
            this->newBestIncumbent = false;

            // Create IPOPT application
            Ipopt::SmartPtr<Ipopt::IpoptApplication> app = IpoptApplicationFactory();

            // Set IPOPT options
            // Use IPOPT-specific values if set, otherwise fall back to SNOPT equivalents
            int max_iter = this->myOptions.get_ipopt_max_iterations();
            if (max_iter < 0)
                max_iter = static_cast<int>(this->myOptions.get_major_iterations_limit());
            app->Options()->SetIntegerValue("max_iter", max_iter);

            double conv_tol = this->myOptions.get_ipopt_convergence_tolerance();
            if (conv_tol < 0)
                conv_tol = this->myOptions.get_optimality_tolerance();
            app->Options()->SetNumericValue("tol", conv_tol);

            double feas_tol = this->myOptions.get_ipopt_constraint_violation_tolerance();
            if (feas_tol < 0)
                feas_tol = this->myOptions.get_feasibility_tolerance();
            app->Options()->SetNumericValue("constr_viol_tol", feas_tol);

            int max_time = this->myOptions.get_ipopt_max_run_time();
            if (max_time < 0)
                max_time = static_cast<int>(this->myOptions.get_max_run_time_seconds());
            app->Options()->SetNumericValue("max_cpu_time", static_cast<double>(max_time));

            // L-BFGS Hessian approximation (no second derivatives)
            app->Options()->SetStringValue("hessian_approximation", "limited-memory");

            // Barrier parameter strategy
            int mu_strategy = this->myOptions.get_ipopt_mu_strategy();
            app->Options()->SetStringValue("mu_strategy",
                mu_strategy == 0 ? "monotone" : "adaptive");

            // Print level
            int print_level = this->myOptions.get_ipopt_print_level();
            if (print_level < 0)
                print_level = this->myOptions.get_quiet_NLP() ? 0 : 5;
            app->Options()->SetIntegerValue("print_level", print_level);

            // Derivative checking
            if (this->myOptions.get_check_derivatives())
                app->Options()->SetStringValue("derivative_test", "first-order");

            // Initialize IPOPT
            Ipopt::ApplicationReturnStatus init_status = app->Initialize();
            if (init_status != Ipopt::Solve_Succeeded)
            {
                std::cout << "IPOPT initialization failed with status " << init_status << std::endl;
                return;
            }

            // Create adapter and solve
            Ipopt::SmartPtr<Ipopt::TNLP> nlp = new EMTG_IPOPT_NLP(this, this->myProblem, this->myOptions);
            this->NLP_start_time = time(NULL);
            this->ipopt_status = app->OptimizeTNLP(nlp);

            // Post-solve: unscale the solution
            this->unscaleX();

            // Check if NLP chaperone found a better point
            if (this->myOptions.get_enable_NLP_chaperone()
                && this->J_NLP_incumbent < EMTG::math::LARGE)
            {
                // Evaluate feasibility of final point
                double feasibility, normalized_feasibility, distance_from_equality_filament, decision_variable_infeasibility;
                size_t worst_constraint_local, worst_decision_variable_local;

                this->myProblem->check_feasibility(this->X_unscaled,
                    this->F,
                    worst_decision_variable_local,
                    worst_constraint_local,
                    feasibility,
                    normalized_feasibility,
                    distance_from_equality_filament,
                    decision_variable_infeasibility);

                this->feasibility_metric = normalized_feasibility;

                // If incumbent is better, use it
                if (this->feasibility_metric_NLP_incumbent < normalized_feasibility
                    || (this->feasibility_metric_NLP_incumbent <= this->myOptions.get_feasibility_tolerance()
                        && this->J_NLP_incumbent < this->F[0]))
                {
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->F = this->F_NLP_incumbent;
                    this->G = this->G_NLP_incumbent;
                    this->unscaleX();
                }
            }

            // Report result
            switch (this->ipopt_status)
            {
                case Ipopt::Solve_Succeeded:
                    std::cout << "IPOPT: Optimal solution found." << std::endl;
                    break;
                case Ipopt::Solved_To_Acceptable_Level:
                    std::cout << "IPOPT: Solved to acceptable level." << std::endl;
                    break;
                case Ipopt::Feasible_Point_Found:
                    std::cout << "IPOPT: Feasible point found." << std::endl;
                    break;
                case Ipopt::Infeasible_Problem_Detected:
                    std::cout << "IPOPT: Problem appears infeasible." << std::endl;
                    break;
                case Ipopt::Maximum_Iterations_Exceeded:
                    std::cout << "IPOPT: Maximum iterations exceeded." << std::endl;
                    break;
                case Ipopt::Maximum_CpuTime_Exceeded:
                    std::cout << "IPOPT: Maximum CPU time exceeded." << std::endl;
                    break;
                case Ipopt::User_Requested_Stop:
                    std::cout << "IPOPT: User requested stop (goal attained or time limit)." << std::endl;
                    break;
                default:
                    std::cout << "IPOPT: Solver returned status " << this->ipopt_status << std::endl;
                    break;
            }
        }

        void IPOPT_interface::update_NLP_incumbent(const std::vector<doubleType>& X_scaled_in,
                                                    const std::vector<doubleType>& F_in,
                                                    const std::vector<double>& G_in,
                                                    double feasibility)
        {
            this->X_NLP_incumbent_scaled = X_scaled_in;
            this->F_NLP_incumbent = F_in;
            this->G_NLP_incumbent = G_in;
            this->J_NLP_incumbent = F_in[0];
            this->feasibility_metric_NLP_incumbent = feasibility;

            // Unscale the incumbent
            for (size_t i = 0; i < this->nX; ++i)
                this->X_NLP_incumbent_unscaled[i] = X_scaled_in[i] * this->myProblem->X_scale_factors[i]
                                                    + this->myProblem->Xlowerbounds[i];

            this->newBestIncumbent = true;

            // Write intermediate solution if better than global incumbent
            if (this->J_NLP_incumbent < this->JGlobalIncumbent)
            {
                try
                {
                    this->myProblem->evaluate(this->X_NLP_incumbent_unscaled, this->F_NLP_incumbent, this->G_NLP_incumbent, false);
                    this->myProblem->output(this->myProblem->options.outputfile);
                }
                catch (...) {}
            }
        }

        void IPOPT_interface::set_final_solution(const std::vector<doubleType>& X_scaled_in,
                                                   Ipopt::SolverReturn status)
        {
            this->X_scaled = X_scaled_in;
            this->unscaleX();

            // Evaluate at final point to populate F and G
            try
            {
                this->myProblem->evaluate(this->X_unscaled, this->F, this->G, false);
            }
            catch (std::exception& error)
            {
                std::cout << "IPOPT: Failed to evaluate final solution: " << error.what() << std::endl;
            }
        }

    } // namespace Solvers
} // namespace EMTG

#endif // USE_IPOPT
```

**Step 3: Commit**

```bash
git add src/InnerLoop/IPOPT_interface.h src/InnerLoop/IPOPT_interface.cpp
git commit -m "feat: add IPOPT_interface class implementing NLP_interface for IPOPT solver"
```

---

## Task 8: Update InnerLoop CMakeLists.txt

**Files:**
- Modify: `src/InnerLoop/CMakeLists.txt`

**Step 1: Add IPOPT source files conditionally**

After the existing source file list, add:

```cmake
if(USE_IPOPT)
    set(INNERLOOP_HEADERS ${INNERLOOP_HEADERS}
        IPOPT_interface.h
        EMTG_IPOPT_NLP.h)
    set(INNERLOOP_SOURCE ${INNERLOOP_SOURCE}
        IPOPT_interface.cpp
        EMTG_IPOPT_NLP.cpp)
endif()
```

The exact location depends on how the CMakeLists.txt is structured — add it after the existing SNOPT source file declarations.

**Step 2: Commit**

```bash
git add src/InnerLoop/CMakeLists.txt
git commit -m "feat: add IPOPT source files to InnerLoop CMakeLists.txt"
```

---

## Task 9: Update problem.cpp Solver Factory

**Files:**
- Modify: `src/Core/problem.cpp` (lines 29, 176, 271, 396)

**Step 1: Add IPOPT include**

After line 29 (`#include "SNOPT_interface.h"`), add:

```cpp
#ifdef USE_IPOPT
#include "IPOPT_interface.h"
#endif
```

**Step 2: Add helper to create NLP solver**

Before the `problem::optimize()` method (around line 66), add a private helper or use inline logic. The simplest approach is to create the right solver inline in each case block.

**Step 3: Update MBH case (line 176)**

Replace:
```cpp
                Solvers::SNOPT_interface mySNOPT(this, myNLPoptions);
                EMTG::Solvers::MBH solver(this, &mySNOPT);
```

With:
```cpp
                std::unique_ptr<Solvers::NLP_interface> myNLP;
#ifdef USE_IPOPT
                if (this->options.NLP_solver_type == 2)
                    myNLP = std::make_unique<Solvers::IPOPT_interface>(this, myNLPoptions);
                else
#endif
                    myNLP = std::make_unique<Solvers::SNOPT_interface>(this, myNLPoptions);
                EMTG::Solvers::MBH solver(this, myNLP.get());
```

**Step 4: Update NLP case (line 271)**

Replace:
```cpp
                Solvers::SNOPT_interface mySNOPT(this, myNLPoptions);
```

With:
```cpp
                std::unique_ptr<Solvers::NLP_interface> myNLP;
#ifdef USE_IPOPT
                if (this->options.NLP_solver_type == 2)
                    myNLP = std::make_unique<Solvers::IPOPT_interface>(this, myNLPoptions);
                else
#endif
                    myNLP = std::make_unique<Solvers::SNOPT_interface>(this, myNLPoptions);
```

Then update all references from `mySNOPT.` to `myNLP->` in this case block:
- `mySNOPT.setX0_unscaled(...)` → `myNLP->setX0_unscaled(...)`
- `mySNOPT.setJGlobalIncumbent(...)` → `myNLP->setJGlobalIncumbent(...)`
- `mySNOPT.run_NLP(...)` → `myNLP->run_NLP(...)`
- `mySNOPT.getX_unscaled()` → `myNLP->getX_unscaled()`

**Step 5: Update FilamentWalker case (line 396)**

Replace:
```cpp
                Solvers::SNOPT_interface mySNOPT(this, myNLPoptions);
                Solvers::FilamentWalker myFilamentWalker(this, &mySNOPT);
```

With:
```cpp
                std::unique_ptr<Solvers::NLP_interface> myNLP;
#ifdef USE_IPOPT
                if (this->options.NLP_solver_type == 2)
                    myNLP = std::make_unique<Solvers::IPOPT_interface>(this, myNLPoptions);
                else
#endif
                    myNLP = std::make_unique<Solvers::SNOPT_interface>(this, myNLPoptions);
                Solvers::FilamentWalker myFilamentWalker(this, myNLP.get());
```

**Step 6: Add `#include <memory>` for `std::unique_ptr`**

**Step 7: Commit**

```bash
git add src/Core/problem.cpp
git commit -m "feat: add solver factory in problem.cpp to select SNOPT or IPOPT based on NLP_solver_type"
```

---

## Task 10: Add Accessor Methods to NLP_interface

**Files:**
- Modify: `src/InnerLoop/NLP_interface.h`

The `EMTG_IPOPT_NLP` adapter needs access to bounds and scaling data from `NLP_interface`. Some of these getters don't exist yet.

**Step 1: Add missing getters to `NLP_interface.h`**

In the public section (after existing getters around line 63), add:

```cpp
            inline std::vector<double> getXlowerbounds() const { return this->Xlowerbounds; }
            inline std::vector<double> getXupperbounds() const { return this->Xupperbounds; }
            inline std::vector<doubleType> getX0_scaled() const { return this->X0_scaled; }
```

These are needed by `EMTG_IPOPT_NLP` to set up variable bounds and initial guess.

**Step 2: Make `NLP_interface` destructor virtual**

Ensure the destructor is virtual (needed since we use `std::unique_ptr<NLP_interface>`). Add if not present:

```cpp
            virtual ~NLP_interface() {}
```

**Step 3: Commit**

```bash
git add src/InnerLoop/NLP_interface.h
git commit -m "feat: add missing accessor methods and virtual destructor to NLP_interface"
```

---

## Task 11: Install IPOPT and Build

**Step 1: Install IPOPT (if not already available)**

```bash
# On Ubuntu/Debian:
sudo apt-get install coinor-libipopt-dev

# Or build from source:
# git clone https://github.com/coin-or/Ipopt.git
# cd Ipopt && mkdir build && cd build
# cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
# make && sudo make install
```

**Step 2: Configure EMTG with IPOPT**

```bash
cd /workspace
cmake -B build -DUSE_IPOPT=ON
```

**Step 3: Build**

```bash
cmake --build build 2>&1 | head -100
```

**Step 4: Fix any compilation errors**

Common issues to watch for:
- Missing includes
- Type mismatches (Ipopt::Index vs size_t)
- `_GETVALUE` macro usage on doubleType
- Const correctness on NLPoptions getters
- Missing forward declarations

**Step 5: Iterate until build succeeds**

---

## Task 12: Run Testatron Tests

**Step 1: Verify SNOPT still works (no regressions from refactoring)**

```bash
cd /workspace/testatron
python testatron.py -e ../bin/EMTGv9 -p ../PyEMTG/ -f solver_options/
```

All tests should pass with SNOPT (NLP_solver_type=0, the default).

**Step 2: Create IPOPT test cases**

Copy a few existing test `.emtgopt` files and modify them to use `NLP_solver_type 2`. Start with simple cases from `transcription_tests/` or `solver_options/`.

**Step 3: Run IPOPT tests**

```bash
python testatron.py -e ../bin/EMTGv9 -c tests/solver_options/IPOPT_test_case
```

**Step 4: Generate IPOPT truth files**

Once tests produce correct results:
```bash
python testatron.py -e ../bin/EMTGv9 --update_truths -c tests/solver_options/IPOPT_test_case
```

**Step 5: Run full regression suite**

```bash
python testatron.py -e ../bin/EMTGv9 -p ../PyEMTG/
```

All 137 tests should pass.

**Step 6: Commit test additions**

```bash
git add testatron/tests/solver_options/
git commit -m "test: add IPOPT solver test cases to testatron"
```

---

## Task 13: Final Cleanup and Documentation

**Step 1: Update CLAUDE.md**

Add IPOPT to the NLP solver documentation, conditional compilation flags, and build instructions.

**Step 2: Final commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with IPOPT build instructions and architecture notes"
```
