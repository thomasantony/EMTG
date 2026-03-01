# IPOPT Integration Design for EMTG

**Date:** 2026-03-01
**Status:** Approved
**Author:** Claude (AI-assisted design)

## Summary

Add IPOPT (Interior Point Optimizer) as an alternative NLP solver to EMTG, alongside the existing SNOPT interface. IPOPT is open-source (EPL 2.0) and freely available, removing the dependency on proprietary SNOPT for users who cannot obtain it.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hessian mode | L-BFGS approximation | EMTG only computes first derivatives. Exact Hessian would require second-derivative computation across the entire codebase. |
| Architecture | TNLP adapter inside NLP_interface subclass | Mirrors SNOPT pattern (snoptProblemExtension). Clean separation of concerns. |
| MBH/FilamentWalker support | Full refactor to NLP_interface* | Required for test compatibility. All called methods already exist on base class. |
| Configuration | IPOPT-specific options + fallback to SNOPT options | Backward compatible. Legacy .emtgopt files work unchanged. |
| Linking | External install + CMake path | Mirrors SNOPT pattern. User installs IPOPT separately. |

## Architecture

### New Files

| File | Purpose |
|------|---------|
| `src/InnerLoop/IPOPT_interface.h` | EMTG-facing solver class, inherits `NLP_interface` |
| `src/InnerLoop/IPOPT_interface.cpp` | Implements `run_NLP()`, option mapping, result extraction |
| `src/InnerLoop/EMTG_IPOPT_NLP.h` | Internal adapter, inherits `Ipopt::TNLP` |
| `src/InnerLoop/EMTG_IPOPT_NLP.cpp` | Implements all TNLP virtual methods |
| `cmake/FindIPOPT.cmake` | CMake module for locating IPOPT installation |

### Class Hierarchy

```
NLP_interface (abstract base, src/InnerLoop/NLP_interface.h)
├── SNOPT_interface (existing, owns snoptProblemExtension)
└── IPOPT_interface (new, owns EMTG_IPOPT_NLP)
        └── EMTG_IPOPT_NLP : Ipopt::TNLP (internal adapter)
```

### Modified Files

| File | Change |
|------|--------|
| `src/InnerLoop/monotonic_basin_hopping.h/.cpp` | `SNOPT_interface*` → `NLP_interface*` |
| `src/InnerLoop/FilamentWalker.h/.cpp` | `SNOPT_interface*` → `NLP_interface*` |
| `src/Core/problem.cpp` | Solver factory based on `NLP_solver_type` |
| `CMakeLists.txt` | `USE_IPOPT` option, IPOPT detection and linking |
| `src/InnerLoop/CMakeLists.txt` | Conditional IPOPT source inclusion |
| `EMTG-Config-template.cmake` | `IPOPT_ROOT_DIR` variable |
| `OptionsOverhaul/list_of_missionoptions.csv` | New IPOPT options, updated NLP_solver_type range |
| `src/InnerLoop/NLPoptions.h/.cpp` | IPOPT-specific option fields |

## Data Translation: F/G Splitting

EMTG uses SNOPT-style combined vectors. IPOPT separates objective from constraints.

### EMTG Internal Format
- `F[0]` = objective function
- `F[1..nF-1]` = constraints
- `G[]` = sparse Jacobian of entire F w.r.t. X (triplet: `iGfun[]`, `jGvar[]`)
- `A[]` = linear Jacobian entries (triplet: `iAfun[]`, `jAvar[]`)

### IPOPT Translation

| IPOPT Method | Source | Transformation |
|-------------|--------|----------------|
| `eval_f()` | `F[0]` | Direct extraction |
| `eval_grad_f()` | G entries where `iGfun[k] == 0` + A entries where `iAfun[k] == 0` | Sparse → dense (length nX) |
| `eval_g()` | `F[1..nF-1]` | Direct extraction (length m = nF-1) |
| `eval_jac_g()` | G entries where `iGfun[k] > 0` + A entries where `iAfun[k] > 0` | Row indices shifted by -1 |
| `eval_h()` | N/A | Returns false (L-BFGS mode) |
| `get_bounds_info()` | `Xlowerbounds/Xupperbounds` (variables), `Flowerbounds[1:]/Fupperbounds[1:]` (constraints) | Split and shift |

### Pre-computation (in EMTG_IPOPT_NLP constructor)

1. Partition `iGfun`/`jGvar` into `obj_G_indices` (iGfun==0) and `con_G_indices` (iGfun>0)
2. Same for `iAfun`/`jAvar`
3. Pre-build constraint Jacobian sparsity pattern with shifted row indices
4. Pre-build dense gradient mapping for objective

### Evaluation Caching

IPOPT provides a `new_x` flag. When `new_x == true`, call `problem->evaluate()`. When `false`, reuse cached F and G arrays. This prevents redundant full mission evaluations.

## Configuration Options

### Updated Options

- `NLP_solver_type`: range changed from `0-1` to `0-2` (0=SNOPT, 1=WORHP, 2=IPOPT)

### New IPOPT-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ipopt_max_iterations` | `int` | `-1` | Max iterations (-1 = use snopt_major_iterations) |
| `ipopt_feasibility_tolerance` | `double` | `-1` | Constraint violation tolerance (-1 = use snopt_feasibility_tolerance) |
| `ipopt_optimality_tolerance` | `double` | `-1` | Convergence tolerance (-1 = use snopt_optimality_tolerance) |
| `ipopt_max_run_time` | `int` | `-1` | Max CPU time seconds (-1 = use snopt_max_run_time) |
| `ipopt_mu_strategy` | `int` | `1` | Barrier parameter: 0=monotone, 1=adaptive |
| `ipopt_linear_solver` | `std::string` | `mumps` | Linear solver (mumps, ma27, ma57, etc.) |
| `ipopt_print_level` | `int` | `-1` | Print level 0-12 (-1 = auto from quiet_NLP) |

### Fallback Logic

When an IPOPT-specific option is `-1` (sentinel), the corresponding SNOPT option value is used. Existing `.emtgopt` files work unchanged with `NLP_solver_type 2`.

## NLP Chaperone

The NLP chaperone (best feasible incumbent tracking) uses IPOPT's `intermediate_callback()`:

1. After each iteration, check feasibility (`inf_pr < tolerance`)
2. If feasible and better than incumbent, update `X_NLP_incumbent`, `F_NLP_incumbent`
3. If `stop_on_goal_attain` and objective meets goal, return `false` to stop
4. Check time limit — return `false` if exceeded
5. Write intermediate solutions for MBH animation

The cached X/F from the most recent `eval_f()`/`eval_g()` call provides the full vectors.

`finalize_solution()` copies final X, lambda, z_L, z_U back to NLP_interface storage. If chaperone incumbent is better, reverts to incumbent.

## Status Code Mapping

| IPOPT Status | EMTG Outcome |
|-------------|--------------|
| `Solve_Succeeded` | Feasible + optimal (SUCCESS) |
| `Solved_To_Acceptable_Level` | Feasible (SUCCESS) |
| `Feasible_Point_Found` | Feasible but not optimal (SUCCESS) |
| `Maximum_Iterations_Exceeded` | Check chaperone incumbent |
| `Maximum_CpuTime_Exceeded` | Check chaperone incumbent |
| `User_Requested_Stop` | From chaperone — check incumbent |
| `Infeasible_Problem_Detected` | FAILURE |
| All other errors | FAILURE |

### FeasiblePoint Mode

When `NLPMode == FeasiblePoint`, set objective to constant 0. IPOPT seeks constraint satisfaction only.

## CMake Integration

### New CMake Module: `cmake/FindIPOPT.cmake`
- Uses `pkg-config` first (standard Linux), falls back to `IPOPT_ROOT_DIR`
- Sets `IPOPT_FOUND`, `IPOPT_INCLUDE_DIRS`, `IPOPT_LIBRARIES`

### Build Configuration
- New option: `USE_IPOPT` (OFF by default)
- IPOPT and SNOPT can both be enabled simultaneously
- All IPOPT code guarded with `#ifdef USE_IPOPT`
- Config variable: `IPOPT_ROOT_DIR` in `EMTG-Config.cmake`

## MBH/FilamentWalker Refactoring

### Changes
- `MBH` constructor: `SNOPT_interface*` → `NLP_interface*`
- `FilamentWalker` constructor: `SNOPT_interface*` → `NLP_interface*`
- Member fields renamed: `mySNOPT` → `myNLP`

### Safety
All methods called by MBH/FilamentWalker (`run_NLP()`, `getX_unscaled()`, `setX0_unscaled()`, `setJGlobalIncumbent()`, etc.) are already defined on `NLP_interface`. The refactor only widens the type. When `NLP_solver_type == 0`, the exact same `SNOPT_interface` object flows through — zero behavioral change for SNOPT users.

### Solver Factory in problem.cpp
```cpp
std::unique_ptr<Solvers::NLP_interface> solver;
if (options.NLP_solver_type == 2) {
    #ifdef USE_IPOPT
    solver = std::make_unique<Solvers::IPOPT_interface>(this, myNLPoptions);
    #else
    throw std::runtime_error("IPOPT not available. Rebuild with USE_IPOPT=ON.");
    #endif
} else {
    solver = std::make_unique<Solvers::SNOPT_interface>(this, myNLPoptions);
}
```

## Testing Strategy

All existing testatron tests must pass when using IPOPT. The approach:
1. Run tests with SNOPT (existing behavior, no changes)
2. Run same tests with `NLP_solver_type 2` to verify IPOPT produces equivalent results
3. IPOPT may find slightly different optimal points (interior-point vs active-set), so test comparison should be against IPOPT-specific truth files if needed
4. Feasibility tolerance matching ensures constraint satisfaction is equivalent
