# EMTG Performance Optimization Analysis

**Date:** 2026-02-28
**Scope:** Comprehensive analysis of performance hotpaths, third-party acceleration opportunities, and emerging techniques applicable to EMTG v9.

---

## Table of Contents

1. [Current Performance Hotpath Analysis](#1-current-performance-hotpath-analysis)
2. [Modified Chebyshev Picard Iteration (MCPI)](#2-modified-chebyshev-picard-iteration-mcpi)
3. [MadNLP.jl — GPU-Accelerated NLP Solver](#3-madnlpjl--gpu-accelerated-nlp-solver)
4. [Other Third-Party Improvements](#4-other-third-party-improvements)
5. [MCPI Implementation Strategy: Rust + GPU](#5-mcpi-implementation-strategy-rust--gpu)
6. [Prioritized Recommendations](#6-prioritized-recommendations)

---

## 1. Current Performance Hotpath Analysis

### 1.1 NLP User Function — The Innermost Hot Loop

The single hottest code path in EMTG is `SNOPT_interface::SNOPT_user_function()` (`src/InnerLoop/SNOPT_interface.cpp:326`), called thousands of times per NLP solve. The call chain is:

```
SNOPT_user_function()          [~0.1% — pointer lookup, vector copy]
  → unscaleX()                 [~0.1% — O(nX) linear transform]
  → problem::evaluate()        [~99%  — THE BOTTLENECK]
    → Mission::evaluate()
      → Journey::process_journey()
        → Phase::process_phase()   ← trajectory propagation happens here
          → Propagator::propagate()
            → ExplicitRungeKutta::step()  [repeated per integration step]
              → EOM::evaluate()           [4× for RK4, 13× for DP87]
                → SpacecraftAccelerationModel::computeAcceleration()
  → wrap F, G vectors          [~0.5% — O(nF + nG)]
  → NLP chaperone logic        [~0.3% — O(nF), optional]
```

**Key metrics:**
- SNOPT calls `evaluate()` 100–10,000 times per NLP solve
- MBH performs 10–1000+ NLP solves per run
- Total `evaluate()` calls per MBH run: 10^4 to 10^7
- SNOPT derivative option is set to `1` (`SNOPT_interface.cpp:123`), meaning finite-difference Jacobian approximation — this may cause redundant `evaluate()` calls if analytical derivatives via STM are already available

### 1.2 Integration Cost Breakdown

**Per-step cost (with STM propagation):**

| Integrator | Stages | EOM calls/step | Typical cost/step |
|-----------|--------|----------------|-------------------|
| RK4       | 4      | 4              | 20–80 μs          |
| DP87      | 13     | 13             | 60–240 μs         |

**Per-phase cost (FBLT, 100-day phase, 50 thrust segments):**
- ~400 EOM evaluations per phase evaluation (RK4, 2 steps/segment)
- With STM: 3–4× overhead for Jacobian propagation through RK stages
- Total: ~5–30 ms per phase depending on force model complexity

**STM propagation structure** (`ExplicitRungeKutta.cpp:82-98`):
- Each RK stage: `STM_stage = (fx * step_size + grad_vec * dstepdState) * STM`
- Cumulative: `STM = STM_left + Σ(STM_stage * A[k])`
- STM is 14×14 for FBLT (state + control variables)
- Cumulative chain stored for distance constraints: O(num_timesteps²) matrix products

### 1.3 Acceleration Model Cost

`SpacecraftAccelerationModel::computeAcceleration()` (`src/Astrodynamics/AccelerationModel/SpacecraftAccelerationModel.cpp`):

| Component | Cost | Notes |
|-----------|------|-------|
| Central force (gravity) | ~1 μs | Point-mass or J2 |
| Spherical harmonics | 5–50 μs | Degree/order dependent |
| Third-body gravity | ~2 μs per body | Requires ephemeris lookup |
| Thrust term | 5–15 μs | Power/throttle calculations |
| SRP | ~1 μs | Solar radiation pressure |
| Aerodynamic drag | ~2 μs | Atmosphere model lookup |
| fx matrix assembly | 2–5 μs | 14×14 Jacobian block |

### 1.4 Parallelism — Current State

**There is NO parallelism in the current codebase.** Zero OpenMP directives, zero threading, zero GPU usage. This is the single largest structural limitation.

Parallelism opportunities:
- **MBH/FilamentWalker outer loop**: Embarrassingly parallel (independent NLP solves). Expected speedup: linear with cores (8–40×).
- **Multi-journey evaluation**: Independent journeys could be evaluated in parallel.
- **SNOPT finite-difference Jacobian**: If SNOPT is perturbing each variable independently, these perturbations are parallelizable (though this is internal to SNOPT).

### 1.5 Memory and Data Patterns

- Decision vectors `X_scaled`, `X_unscaled`: dense, linear access — cache-friendly
- Jacobian `G[]`: sparse, scattered writes via `iGfun`/`jGvar` — potential cache misses
- Ephemeris lookups: SplineEphem or CSpice calls per body per EOM evaluation — cache-unfriendly if many bodies
- EMTG_Matrix: custom implementation without SIMD; all matrix operations are scalar loops

### 1.6 Scalatron Scaling

Optional NLP scaling framework (`src/Scalatron/`). Called before each NLP solve in MBH (`monotonic_basin_hopping.cpp:330-342`). Improves convergence by adapting variable/constraint scales, but adds modest overhead per MBH iteration.

---

## 2. Modified Chebyshev Picard Iteration (MCPI)

### 2.1 Core Idea

MCPI (Bai & Junkins, 2011) is a fundamentally different approach to trajectory propagation. Instead of marching forward step-by-step (as RK4/DP87 do), MCPI:

1. **Approximates the entire trajectory segment** as a truncated Chebyshev polynomial series
2. **Iteratively refines** the approximation using Picard iteration (fixed-point iteration on the integral form of the ODE)
3. **Converts the propagation to matrix operations**: The Picard update at each iteration is a matrix-vector multiplication of pre-computed constant matrices (T, V, S) against force evaluations at Chebyshev nodes

The method can be expressed as:

```
x_{k+1}(t) = x(t_0) + ∫[t_0..t] f(τ, x_k(τ)) dτ
```

where the integral is evaluated via Clenshaw-Curtis quadrature on Chebyshev nodes, and the entire operation reduces to matrix multiplication.

### 2.2 Why It Matters for EMTG

**GPU amenability:** The force function evaluations at all Chebyshev nodes within a segment are independent (they only depend on the current iteration's trajectory approximation). This means:
- All N node evaluations can be computed in parallel (SIMD/GPU)
- The matrix multiplication is a standard BLAS operation (highly optimized on GPUs)
- Multiple trajectory segments or Monte Carlo variants can be batched

**Published results:**
- Bai & Junkins demonstrated GPU implementation in CUDA with CUBLAS (2010)
- Comparable or superior precision to 12th-order RK-Nyström
- 11+ digit accuracy over 1–3 orbit periods
- Woollands et al. (2023) achieved GPU propagation of large sets of initial conditions via "Picard-Chebyshev augmentation"
- Woollands et al. (2024) addressed bang-bang low-thrust dynamics specifically

### 2.3 STM Computation via MCPI

Read, Younes et al. (2015) demonstrated MCPI-based STM propagation:
- Both trajectory and STM computed simultaneously within the Picard iteration framework
- Maximum relative error < 10^-14 (satisfies STM group and symplectic properties)
- Works with high-fidelity spherical harmonic gravity (EGM2008)
- The STM computation naturally parallelizes alongside the trajectory

This is directly relevant to EMTG, which needs STMs for NLP Jacobian construction.

### 2.4 Low-Thrust Applications

- Koblick et al. (2016): Used MCPI as propagator within SNOPT-based low-thrust optimization, demonstrating faster runtime than Adams-Bashforth-Moulton and Gauss-Jackson 8th-order methods
- Woollands et al. (2024): Adaptive Picard-Chebyshev for bang-bang control dynamics — directly addresses the on/off thrust switching that EMTG's FBLT phases encounter
- Method of Particular Solutions (MPS): MCPI variant that avoids explicit STM computation for boundary-value problems (could benefit EMTG's two-point shooting formulation)

### 2.5 Limitations

- **Long-arc instability**: MCPI requires segmentation for trajectories spanning many orbits. The polynomial degree and segment length must be tuned (Woollands' adaptive method addresses this).
- **Non-smooth dynamics**: Discontinuities in thrust (bang-bang control) require special treatment — the 2024 Woollands paper addresses this but adds complexity.
- **Integration effort**: EMTG's integrator framework (`IntegrationScheme` → `ExplicitRungeKutta`) would need a new `PicardChebyshevScheme` implementation. The `Integrand` interface could be reused, but the propagator loop in `IntegratedFixedStepPropagator` would need restructuring since MCPI does not march step-by-step.
- **No existing C++ library**: The Texas A&M implementations are MATLAB and CUDA. A C++/CUDA implementation would need to be written or ported.

### 2.6 Assessment for EMTG

| Criterion | Score | Notes |
|-----------|-------|-------|
| Performance gain (serial) | Moderate | Comparable to high-order RK for single trajectories |
| Performance gain (GPU) | **High** | Massively parallel force evaluations + BLAS matrix ops |
| STM support | Yes | Demonstrated with full J2+ gravity |
| Low-thrust support | Yes | Recent work on bang-bang dynamics |
| Integration complexity | **High** | Requires new propagator type; no C++ library exists |
| Risk | Medium-High | Novel method, limited production usage |

**Verdict:** MCPI is the most promising path to GPU-accelerated propagation in EMTG, but requires significant development effort. The payoff is primarily for problems with many segments/phases or when running large Monte Carlo studies. For single NLP solves, the serial speedup over DP87 is modest. **Best suited as a medium-term research investment.**

---

## 3. MadNLP.jl — GPU-Accelerated NLP Solver

### 3.1 What MadNLP.jl Is

MadNLP.jl is an open-source nonlinear programming solver implemented in Julia. It uses a filter line-search interior-point method (same algorithmic family as IPOPT, distinct from SNOPT's SQP approach). Key features:

- Full GPU support via CUDA (requires NVIDIA GPU)
- Operates on data structures in GPU memory — avoids CPU↔GPU transfers
- Supports sparse NLP problems (EMTG's problem structure)
- Compatible with ExaModels.jl for automatic model building on GPU
- Published benchmarks show 10–25× speedup over CPU IPOPT for large problems

### 3.2 Benchmark Results

**Distillation column benchmark (Pacaud & Shin, 2024):**
- Problem sizes: N=100 to N=50,000 (up to 3.3M variables)
- GPU (NVIDIA A30): 25× faster per IPM iteration vs. CPU (HSL ma27)
- Same iteration count as CPU — pure computational speedup
- Lifted-KKT method: 26× faster than HSL ma27

**AC Optimal Power Flow (2024–2025):**
- 70K+ node power grid optimization
- 10× speedup over state-of-the-art CPU solvers
- First real-time solution (<20s) for problems of this scale on NVIDIA A100

**CUTEst benchmark suite:**
- MadNLP matches IPOPT in iteration count and robustness
- 3× speedup with structure-exploiting parallel linear algebra (CPU)
- 10× speedup for dense NLP problems (GPU)

### 3.3 Integration Feasibility with EMTG

**The fundamental challenge: MadNLP is Julia, EMTG is C++.**

Integration paths:

| Approach | Effort | Risk | Notes |
|----------|--------|------|-------|
| Julia C API (`jl_init`, `jl_call`) | High | High | Embed Julia runtime in C++ process; GC interaction issues |
| PackageCompiler.jl shared library | High | Medium | Compile Julia solver into `.so`; still needs Julia runtime |
| Subprocess (`.emtgopt` → Julia → results) | Low | Low | Loose coupling; only practical for outer-loop replacement |
| Full Julia rewrite of evaluate() | Very High | Very High | Defeats purpose of C++ codebase |

**Key concern: The evaluate() function is in C++.** MadNLP's GPU advantage comes from having BOTH the solver AND the problem evaluation on GPU. If `evaluate()` stays in C++ on CPU, every function evaluation requires CPU↔GPU data transfer, likely negating the GPU speedup.

For MadNLP's GPU acceleration to truly help, the **entire NLP problem** (decision variables, constraints, Jacobian computation) would need to be reformulated in Julia/ExaModels — essentially a rewrite.

### 3.4 SNOPT vs. Interior-Point Methods

EMTG uses SNOPT (SQP method). MadNLP/IPOPT use interior-point methods. Key differences:

| Property | SNOPT (SQP) | MadNLP/IPOPT (IPM) |
|----------|-------------|---------------------|
| Warm-starting | Excellent | Poor (IPM starts from interior) |
| Sparse small problems | Fast | Overhead from barrier formulation |
| Large dense problems | Slow | Fast (GPU-amenable linear algebra) |
| MBH compatibility | Good (warm-start between slides) | Problematic (no effective warm-start) |
| License | Commercial | Open-source |

**SNOPT's warm-starting is critical for MBH performance.** Each MBH slide starts from the previous solution, and SNOPT exploits the basis information. IPM solvers like MadNLP cannot warm-start effectively, which could increase MBH total time even if individual solves are faster.

### 3.5 Assessment for EMTG

| Criterion | Score | Notes |
|-----------|-------|-------|
| Performance gain | Low-Medium | GPU advantage requires full problem on GPU |
| Integration complexity | **Very High** | Julia↔C++ interop; full evaluate() rewrite needed |
| MBH compatibility | **Poor** | No warm-starting; hurts MBH workflow |
| Sparse problem handling | Good | Supports sparse Jacobians |
| Maturity | Medium | Active development; used in power systems |
| Risk | High | Language barrier; architectural mismatch |

**Verdict:** MadNLP.jl is not a practical path for EMTG acceleration. The C++→Julia integration overhead, lack of warm-starting for MBH, and requirement to have the entire problem on GPU make it a poor fit. The effort would be comparable to rewriting EMTG in Julia.

### 3.6 Better Alternative: HiOp (C++ GPU NLP Solver)

If GPU-accelerated NLP solving is desired, **HiOp** (Lawrence Livermore National Laboratory) is a far better fit than MadNLP:

- **Native C++** — no language boundary issues; integration follows the same pattern as SNOPT/WORHP
- **GPU support** via NVIDIA CUDA and AMD ROCm, using the RAJA portability layer
- **Problem types:** General sparse NLPs, mixed dense-sparse NLPs, quasi-Newton for dense-constrained problems
- **License:** BSD 3-clause (permissive)
- **C++ API:** Object-oriented `hiopInterfaceSparse` for general sparse NLPs
- **Integration:** A `HiOp_interface` class inheriting from `NLP_interface` is architecturally straightforward
- **GitHub:** https://github.com/LLNL/hiop

Other C++ GPU-capable options:
- **IPOPT + SPRAL:** GPU-accelerated sparse indefinite linear solver, officially supported
- **IPOPT + Re::Solve** (Oak Ridge, 2025): GPU-native direct and iterative solvers for IPOPT

However, the fundamental concern remains: EMTG's typical problem sizes (hundreds to low thousands of variables) are too small for GPU NLP solving to provide meaningful benefit. GPU acceleration of the **propagation** (via MCPI) is a better use of GPU hardware for EMTG.

---

## 4. Other Third-Party Improvements

### 4.1 Eigen Library (Replace EMTG_Matrix)

**What:** Replace custom `EMTG::math::Matrix` with Eigen 3.x for all linear algebra.

**Why — current EMTG_Matrix limitations** (`src/Math/EMTG_Matrix.h/.cpp`):
- Uses `std::vector<T>` backing store (heap-allocated, even for 3×1 vectors)
- Naive triple-loop matrix multiplication with no SIMD, no unrolling (`EMTG_Matrix.cpp:937-962`)
- All operator overloads are `virtual`, preventing compiler inlining — a significant penalty
- No expression templates: every intermediate result allocates a temporary matrix
- Row-major layout (Eigen defaults to column-major, which is better for BLAS compatibility)

**What Eigen provides:**
- **Fixed-size matrices** (`Eigen::Matrix<double,14,14>`): stack allocation, compile-time unrolling
- **SIMD vectorization**: SSE2/AVX/AVX-512 automatically, measured 4.6× speedup on small matrices
- **Expression templates**: `(A + B).transpose() * C` compiles to a single fused loop, zero temporaries
- **Cache-optimized blocking** for matrix products
- For small matrices (2–14 dimensions): **2–10× speedup** over naive `std::vector`-based implementations

**Expected impact:**
- 2–5× speedup on matrix operations (STM products, fx assembly)
- Since matrix ops are ~30–40% of EOM evaluation cost, overall speedup: 15–40% on propagation
- Larger gains for phases with many segments (cumulative STM chain: O(N²) 14×14 products)
- Compile with `-O3 -march=native -DNDEBUG` for maximum benefit

**Integration effort:** Medium. Phased migration recommended:
1. Start with hot-path code: 14×14 STM multiplications in `ExplicitRungeKutta`
2. Replace `math::Matrix<double>` with `Eigen::Matrix<double,14,14>` for fixed-size STMs
3. Handle EMTG_Matrix's 1-based indexing vs. Eigen's 0-based
4. Verify AD compatibility (`doubleType` template with Eigen's custom scalar support)
5. Migrate remaining matrix operations incrementally

**Risk:** Low. Eigen is header-only, well-tested, and widely used in robotics/astrodynamics (PSOPT, ifopt, Ceres Solver).

### 4.2 IPOPT as SNOPT Alternative

**What:** Add IPOPT (Interior Point OPTimizer) as a second NLP solver option.

**Why:**
- Open-source (no commercial license needed)
- Well-maintained, active development
- Supports warm-starting (better than MadNLP, though not as good as SNOPT)
- Could enable users without SNOPT license to run EMTG
- EMTG already has the `NLP_interface` abstraction (`src/InnerLoop/NLP_interface.h`); adding a new solver implementation follows the existing pattern

**Expected impact:** Not primarily a performance improvement, but a usability/accessibility win. IPOPT may be faster than SNOPT for certain problem classes (large, well-scaled problems).

**Integration effort:** Medium. Implement `IPOPT_interface` following the pattern of `SNOPT_interface`. IPOPT's C++ API is well-documented.

**Risk:** Low. IPOPT is mature and widely used.

### 4.3 OpenMP Parallelization of MBH

**What:** Parallelize the outer MBH loop (`monotonic_basin_hopping.cpp:479-813`).

**Why:**
- Each MBH hop-slide-evaluate cycle is independent
- FilamentWalker similarly runs independent NLP solves
- This is embarrassingly parallel — no algorithmic changes needed
- Each thread gets its own `SNOPT_interface` + `problem` instance

**Published validation:** McCarty and McGuire (NASA, 2018) published "Parallel Monotonic Basin Hopping for Low Thrust Trajectory Optimization" directly addressing this. They demonstrated near-linear scaling with a head-node/worker architecture, where workers run independent MBH instances and asynchronously share improved solutions.

**Expected impact:**
- Near-linear speedup with number of cores: 8× on 8-core, 32× on 32-core
- For MBH runs with 1000+ iterations, this is the single largest performance win available
- McCarty & McGuire found PMBH "more quickly finds feasible solutions and improves locally optimal solutions"

**Integration effort:** Medium-Low. Three approaches in increasing order of effort:
1. **Zero-code option**: Launch N independent EMTG processes with different seeds, post-process for best
2. **OpenMP** (`#pragma omp parallel`): Each thread gets its own `problem` + `SNOPT_interface` deep copy; shared best solution via `#pragma omp critical`
3. **MPI** (as in McCarty & McGuire): Head-node/worker architecture with asynchronous solution sharing

Key concern: SNOPT's Fortran global state is not thread-safe. Options 2–3 require separate SNOPT instances per thread/process.

**Risk:** Low-Medium. Main risk is ensuring deep-copy correctness for `problem` objects.

### 4.4 Alternative AD Libraries (CoDiPack, Adept, Enzyme)

**What:** Replace or supplement GSAD with a more performant AD library.

EMTG's current AD (`#ifdef AD_INSTRUMENTATION` → GSAD `adouble`) provides forward-mode AD via operator overloading. GSAD is an internal NASA Goddard tool with no public documentation or external benchmarks. The `EMTG_Matrix` class's `virtual` operator overloads further penalize AD performance by preventing compiler inlining.

Alternatives, ranked by measured performance:

| Library | Mode | Approach | Relative Speed | GPU | License |
|---------|------|----------|----------------|-----|---------|
| **FastAD** | Forward+Reverse | Expression templates | Fastest (2–10× faster than Adept) | No | MIT |
| **Adept 2** | Forward+Reverse | Expression templates + arrays | 2.6–9× faster than ADOLC/CppAD | No | Apache 2.0 |
| **CoDiPack** | Forward+Reverse | Jacobian taping + expr. templates | Comparable to Adept | No | BSD-3 |
| CppAD | Forward+Reverse | Operator overloading | Baseline | No | EPL 2.0 |
| ADOLC | Forward+Reverse | Primal value taping | Slowest of major libraries | No | EPL/GPL |
| **Enzyme** | Forward+Reverse | LLVM IR transformation | Fastest possible (native code speed) | **Yes** | Apache 2.0 |
| GSAD (current) | Forward | Operator overloading | Unknown (no benchmarks) | No | Internal |

**CoDiPack** is the most practical choice for EMTG: well-documented, actively maintained, BSD-3 licensed, supports both forward and reverse mode, handles MPI-parallel taping, and was designed for large-scale scientific codes (validated on the SU2 CFD solver). Migration path: replace `GSAD::adouble` with `codi::RealForward` or `codi::RealReverse`.

**Enzyme** is the most transformative: it works at the LLVM IR level, differentiating compiled code without source modification. It achieves native code speed (no operator overloading overhead) and could enable GPU-compatible AD for EMTG without rewriting the force model.

**Expected impact:**
- Replacing GSAD with CoDiPack/Adept: **3–10× speedup** on derivative computation
- Reverse-mode AD for constraint Jacobians: significant reduction when many constraints, few variables
- Enzyme: near-zero AD overhead + GPU differentiation

**Integration effort:** Medium (CoDiPack — type replacement), High (Enzyme — LLVM toolchain integration).

### 4.5 Differential Algebra (DA) / Taylor Polynomial Propagation

**What:** Use DACE (Differential Algebra Computational Engine) for high-order Taylor expansion propagation.

**Why:**
- DA propagation computes the Taylor expansion of the flow map to arbitrary order
- A single DA propagation gives not just the STM (1st-order sensitivity) but also higher-order state transition tensors (STTs)
- Useful for uncertainty propagation, conjunction assessment, and manifold computation
- The DACE library is C++, well-maintained (ESA-funded), and compatible with standard integrators

**Expected impact:** Not a direct speedup for the NLP inner loop, but enables:
- Higher-fidelity sensitivity analysis without repeated perturbation propagations
- Monte Carlo uncertainty quantification via polynomial evaluation (orders of magnitude faster than re-propagation)
- Better initial guess generation for NLP

**Integration effort:** Medium. DACE provides a `DA` type that can replace `double` in existing code (similar to GSAD's approach).

### 4.6 Direct Collocation Transcription

**What:** Add Hermite-Simpson or Gauss-Lobatto collocation as an alternative to EMTG's current shooting methods.

**Why:**
- EMTG's current transcriptions (MGALT, FBLT, PSFB, PSBI) use shooting methods that require forward propagation + STM
- Direct collocation discretizes the trajectory as polynomial segments and enforces dynamics as constraints (no propagation needed)
- The NLP Jacobian is analytically sparse and banded — no STM chain multiplication
- Collocation methods are the standard in tools like GPOPS-II, PSOPT, and ALTRO

**Expected impact:**
- Eliminates propagation entirely from the NLP inner loop (dynamics enforced via defect constraints)
- Jacobian computation is local (each segment only depends on adjacent nodes) — inherently parallel
- May improve convergence for stiff or highly nonlinear problems
- GPU-amenable: all defect constraint evaluations are independent

**Integration effort:** High. Requires a new phase type following EMTG's factory pattern.

**Risk:** Medium. Well-understood mathematically, but changes the optimization problem structure significantly. Would coexist with existing shooting methods as an option.

### 4.7 Differentiable Programming (JAX / Julia Ecosystem)

**Emerging approaches in the astrodynamics community (2024–2025):**

- **NASA (AAS 25-290):** Q-law low-thrust guidance using JAX automatic differentiation
- **Google Trajax:** Differentiable optimal control on accelerators (CPU/GPU/TPU), with custom efficient differentiation through solvers
- **ALTRO.jl:** Augmented Lagrangian trajectory optimizer in Julia with ForwardDiff.jl
- **DifferentiableTrajectoryOptimization.jl:** Applies implicit function theorem to differentiate through KKT conditions
- **ESA Neural ODEs:** Neural ODE surrogates for atmospheric/dynamics modeling

These approaches share a theme: using automatic differentiation frameworks to avoid hand-derived Jacobians, enabling GPU acceleration for free.

**Relevance to EMTG:** These are mostly in Python/Julia ecosystems and represent a different architectural philosophy. They're relevant as competitive benchmarks rather than direct integration targets. However, Enzyme (Section 4.4) could bring similar AD-everywhere benefits to C++ code.

### 4.8 Ephemeris Caching / Batching

**What:** Batch or cache SplineEphem/CSpice lookups across integration steps.

**Why:**
- Each EOM evaluation queries ephemeris for Sun, central body, and third-body positions
- Lookups are O(log N) for spline interpolation, but involve memory indirection
- For a fixed-step integrator, the query times are known in advance

**Expected impact:** 10–20% reduction in EOM cost for multi-body force models. Minor for simple two-body problems.

**Integration effort:** Low. Pre-compute ephemeris at all integration times before the step loop.

### 4.9 Verify SNOPT Derivative Mode

**What:** Ensure SNOPT uses user-supplied analytical Jacobian rather than finite differencing.

Currently `SNOPT_interface.cpp:123` sets `setIntParameter("Derivative option", 1)`, which tells SNOPT to estimate derivatives via finite differences. If EMTG is already computing the Jacobian (via STM propagation or AD), SNOPT should be configured with `"Derivative option" = 0` (user-supplied) or `1` (verify user Jacobian via FD, then trust it).

**Expected impact:** If currently using FD: eliminating N_x extra `evaluate()` calls per major iteration. Could be 10–100× reduction in function evaluations if the analytical Jacobian is accurate.

**Integration effort:** Very Low. Single parameter change + verification.

**Risk:** Very Low. This is likely already correct (SNOPT may use FD only for verification), but worth confirming.

---

## 5. MCPI Implementation Strategy: Rust + GPU

### 5.1 The Proposal: Rust + burn Framework → C++ FFI → EMTG

The idea is to implement MCPI as a standalone Rust library using the burn deep learning
framework's GPU compute shaders, then bind it into EMTG's C++ codebase via FFI.

### 5.2 burn Framework Assessment

**burn** (https://github.com/tracel-ai/burn) is a Rust tensor/deep learning framework with
multiple GPU backends via **CubeCL** (their compute language). CubeCL compiles `#[cube]`-annotated
Rust functions into CUDA, ROCm, Vulkan, Metal, and WebGPU kernels.

**Critical finding: f64 (double precision) is problematic on GPU backends.**

| Backend | f64 Support | Status |
|---------|------------|--------|
| burn-ndarray (CPU) | Yes | Stable |
| burn-candle (CPU) | Yes | Stable |
| burn-cuda (NVIDIA GPU) | Partial/Unknown | `Cuda<f64>` type exists but untested for scientific workloads |
| burn-wgpu (WebGPU/Vulkan) | **No** | WGSL spec lacks f64; `naga_ext_f64` is experimental |
| burn-tch (libtorch) | Yes | Wraps PyTorch; not a Rust-native path |

The WebGPU specification does **not support f64** (tracked as gpuweb/gpuweb#2805, deferred to
"Milestone 4+"). This eliminates burn's primary portable backend (WGPU) for astrodynamics.

The CUDA backend (`burn-cuda`) is generic over float type (`Cuda<F = f32, I = i32>`), but
there is no documentation confirming robust f64 support, no scientific computing benchmarks,
and the backend is labeled "experimental."

**burn's MATMUL kernels compete with cuBLAS** — Tracel AI published benchmarks showing their
CubeCL matrix multiply matching NVIDIA's cuBLAS performance. However, these benchmarks are
at f32 precision for ML workloads, not f64 for scientific computing.

### 5.3 The Core Problem: Wrong Abstraction Layer

MCPI reduces trajectory propagation to these operations:
1. **Force function evaluations** at N Chebyshev nodes (embarrassingly parallel)
2. **Matrix-vector multiply**: V × f_eval → Chebyshev coefficients (~60×60 for typical N)
3. **Picard update**: S × coefficients → updated state trajectory
4. **Convergence check**: Compare iterations

Operations 2–3 are matrix multiplications. But the matrices are **small** (typically 40–80
nodes, so 60×60 to 80×80). For individual trajectory segments:

| Operation | Matrix Size | GPU Overhead | GPU Compute | CPU Compute |
|-----------|-----------|-------------|-------------|-------------|
| Single MCPI matmul | 60×60 | ~7–15 μs (launch + PCIe) | ~0.01 μs | ~0.5 μs |
| Force eval at 60 nodes | 60 × O(force model) | ~5 μs (launch) | ~1 μs | ~60 μs |

**For a single trajectory segment, the GPU loses** — kernel launch overhead dominates.

**GPU wins ONLY when batching:** propagating 100+ trajectory segments simultaneously
(e.g., during MBH population evaluation, Monte Carlo, or multi-segment phases).

### 5.4 Alternative: Rust + faer (CPU) — More Practical

The **faer** library (https://github.com/sarah-ek/faer-rs) is a pure-Rust dense linear
algebra library specifically optimized for small matrices:

- **14×14 matmul**: ~0.05–0.1 μs (stack-allocated, AVX2 SIMD, zero allocation)
- **60×60 matmul**: ~0.5–1 μs (still fits in L1 cache)
- Matches or exceeds OpenBLAS/MKL for matrices below ~100×100
- Full f64 support, pure Rust, no external dependencies
- **Competitive with Eigen** for fixed-size small matrices

For MCPI's typical matrix sizes, faer on CPU is **100× faster than burn on GPU** per
individual operation due to eliminated launch/transfer overhead.

### 5.5 Rust → C++ FFI: Feasible and Negligible Overhead

The FFI story is clean. Two approaches:

**Option A: `extern "C"` + cbindgen (Recommended)**
```rust
// Rust side
#[no_mangle]
pub extern "C" fn mcpi_propagate(
    initial_state: *const f64,  // 14 elements (state + epoch + mass + ...)
    t0: f64,
    tf: f64,
    control: *const f64,        // 3 × N_segments thrust vectors
    n_segments: usize,
    out_state: *mut f64,        // 14 elements
    out_stm: *mut f64,          // 196 elements (14×14 flattened)
) -> i32 { ... }
```

```cpp
// C++ side (auto-generated header via cbindgen)
extern "C" int32_t mcpi_propagate(
    const double* initial_state, double t0, double tf,
    const double* control, size_t n_segments,
    double* out_state, double* out_stm);
```

**Option B: `cxx` crate** — Type-safe bridge, slightly more complex but prevents pointer misuse.

**FFI overhead: ~2–5 ns per call** (one function pointer indirection). With propagation
taking ~100 μs, this is **0.002%** overhead — completely negligible.

**Build system**: Use **Corrosion** (https://github.com/corrosion-rs/corrosion) to integrate
Cargo into EMTG's CMake build:

```cmake
include(FetchContent)
FetchContent_Declare(Corrosion GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git)
FetchContent_MakeAvailable(Corrosion)
corrosion_import_crate(MANIFEST_PATH rust_propagator/Cargo.toml)
target_link_libraries(EMTGv9 PRIVATE rust_propagator)
```

### 5.6 Proposed Architecture

```
EMTG C++ codebase
  └── src/Propagation/MCPIPropagator.h/.cpp    ← C++ wrapper (implements PropagatorBase)
        └── extern "C" mcpi_propagate()         ← FFI boundary
              └── rust_propagator crate
                    ├── src/lib.rs              ← FFI entry points
                    ├── src/mcpi.rs             ← Core MCPI algorithm
                    ├── src/chebyshev.rs        ← Chebyshev polynomial operations
                    ├── src/force_model.rs      ← Force model trait + implementations
                    └── Cargo.toml
                          └── dependencies:
                                faer = "0.20"       ← Small matrix linear algebra
                                rayon = "1.10"      ← CPU parallelism for batch mode
```

**Phase 1 (CPU, faer):**
- Implement MCPI with faer for matrix operations
- Single-segment propagation: replace `IntegratedFixedStepPropagator` for CoastPhase
- Validate against RK8(7) reference solutions
- Expected speedup: modest for single propagations; significant via `rayon` for batch MBH

**Phase 2 (GPU, burn-cuda or cudarc):**
- Add batched propagation: propagate 100+ trajectories simultaneously on GPU
- Use `cudarc` (safe Rust CUDA bindings) for direct cuBLAS access to batched GEMM
- Only triggered when MBH/NSGAII requests batch evaluation
- Expected speedup: 10–100× for batch workloads

**Phase 3 (STM + low-thrust):**
- Extend MCPI to propagate STM alongside state (Read et al. 2015 method)
- Implement adaptive segment sizing (Woollands 2024) for thrust arcs
- Wire into FBLT/PSFB phase types

### 5.7 Why Rust Over C++ for This?

| Factor | Rust | C++ (CUDA directly) |
|--------|------|---------------------|
| Memory safety | Guaranteed (no segfaults in propagator) | Manual management |
| Parallelism | `rayon` data parallelism with zero-cost safety | OpenMP (easy to get wrong) |
| Build isolation | Separate Cargo crate; doesn't touch EMTG headers | Deeply entangled with EMTG build |
| Testing | `cargo test` runs independently of EMTG build | Needs full EMTG build to test |
| AD compatibility | Enzyme-AD for Rust is in development | Enzyme-AD for C++ is more mature |
| CUDA access | `cudarc` crate (safe) or `burn-cuda` | Native, no wrapper needed |
| Code reuse | Standalone library usable outside EMTG | Tightly coupled to EMTG types |

The strongest argument for Rust: **build isolation**. The MCPI propagator can be developed,
tested, and benchmarked as a standalone `cargo` project without touching EMTG's CMake build
until integration time. This dramatically reduces development risk.

The strongest argument against: **EMTG team familiarity**. If the team doesn't know Rust,
the maintenance burden falls on whoever wrote it.

### 5.8 Verdict

**burn specifically is the wrong tool.** It's an ML framework with immature f64 GPU support.
But the broader idea — Rust for MCPI propagation — has merit:

- **Phase 1 (Rust + faer, CPU)**: Practical, isolatable, testable. Moderate speedup.
- **Phase 2 (Rust + cudarc, GPU batch)**: The real payoff. Batched cuBLAS GEMM for MCPI.
- **Alternative**: Skip Rust entirely, write MCPI in C++ with Eigen + cuBLAS. Simpler if
  the team is C++-native. The math doesn't care what language it's in.

---

## 6. Prioritized Recommendations

### 6.1 Tier 1: High Impact, Low Effort (Do First)

| # | Improvement | Effort | Expected Speedup | Risk |
|---|-----------|--------|-----------------|------|
| 1 | **Verify SNOPT derivative mode** | Days | 10–100× fewer evals (if FD is on) | Very Low |
| 2 | **OpenMP parallelize MBH** | 2–4 weeks | 8–32× (scales with cores) | Low |
| 3 | **Ephemeris caching** | 1–2 weeks | 10–20% on EOM | Very Low |

### 6.2 Tier 2: High Impact, Medium Effort

| # | Improvement | Effort | Expected Speedup | Risk |
|---|-----------|--------|-----------------|------|
| 4 | **Eigen library** (replace EMTG_Matrix) | 4–8 weeks | 15–40% on propagation | Low |
| 5 | **IPOPT solver** (open-source alternative) | 4–6 weeks | Accessibility win | Low |

### 6.3 Tier 3: Transformative, High Effort (Research Investment)

| # | Improvement | Effort | Expected Speedup | Risk |
|---|-----------|--------|-----------------|------|
| 6 | **MCPI propagation** (GPU-accelerated) | 3–6 months | 10–100× for batch propagation | Medium |
| 7 | **Direct collocation transcription** | 3–6 months | Eliminates propagation from NLP | Medium |
| 8 | **Enzyme AD** (LLVM-based, GPU) | 2–4 months | GPU derivatives for free | Medium-High |

### 6.4 Tier 4: Not Recommended for EMTG

| # | Approach | Why Not |
|---|---------|---------|
| 9 | **MadNLP.jl** | C++↔Julia barrier; requires full problem on GPU; no warm-start for MBH |
| 10 | **Full JAX/Julia rewrite** | Effort equivalent to new tool; loses 15+ years of validated C++ |
| 11 | **CppAD/ADOL-C tape-based AD** | High integration effort for modest gain over GSAD + STM |

### 6.5 Combined Impact Estimate

Implementing Tiers 1 + 2 (realistic 3-month effort):
- **MBH throughput:** 8–32× from OpenMP parallelization
- **Per-evaluation speed:** 25–50% improvement from Eigen + ephemeris caching + derivative mode verification
- **Combined MBH wallclock improvement:** 10–50× depending on hardware

Adding Tier 3 (6–12 month research program):
- **GPU-accelerated propagation via MCPI:** Additional 10–100× for batch/Monte Carlo workloads
- **Direct collocation:** Fundamental improvement in NLP structure; faster convergence for many problem classes

---

## References

### MCPI / Picard-Chebyshev
- Bai, X. & Junkins, J.L. (2011). "Modified Chebyshev-Picard Iteration Methods for Orbit Propagation." *J. Astronaut. Sci.* [Springer](https://link.springer.com/article/10.1007/BF03321533)
- Junkins, J.L. et al. (2013). "Picard Iteration, Chebyshev Polynomials and Chebyshev-Picard Methods: Application in Astrodynamics." *J. Astronaut. Sci.* [Springer](https://link.springer.com/article/10.1007/s40295-015-0061-1)
- Read, J.L. et al. (2015). "State Transition Matrix for Perturbed Orbital Motion Using Modified Chebyshev Picard Iteration." *J. Astronaut. Sci.* [Springer](https://link.springer.com/article/10.1007/s40295-015-0051-3)
- Woollands, R.M. et al. (2023). "GPU-based high-precision orbital propagation of large sets of initial conditions through Picard-Chebyshev augmentation." *Acta Astronautica.* [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0094576522007093)
- Woollands, R.M. et al. (2024). "Adaptive Picard-Chebyshev Methods for Bang-Bang Control Dynamics." *AIAA SciTech Forum.* [AIAA](https://arc.aiaa.org/doi/10.2514/6.2024-1282)
- Koblick, D.C. et al. (2016). "Low Thrust Minimum Time Orbit Transfer Nonlinear Optimization Using Impulse Discretization via the Modified Picard-Chebyshev Method." *CMES.* [TechScience](https://www.techscience.com/CMES/v111n1/27312)

### MadNLP.jl
- Shin, S. et al. (2020). "MadNLP.jl: A Mad Nonlinear Programming Solver." GitHub. [Repository](https://github.com/MadNLP/MadNLP.jl)
- Pacaud, F. & Shin, S. (2024). "GPU-accelerated dynamic nonlinear optimization with ExaModels and MadNLP." [arXiv:2403.15913](https://arxiv.org/abs/2403.15913)
- MadNLPBenchmark.jl. [Repository](https://github.com/MadNLP/MadNLPBenchmark.jl)

### Differentiable Trajectory Optimization
- Google Trajax. [Repository](https://github.com/google/trajax)
- Howell, T.A. et al. "ALTRO.jl: Augmented Lagrangian TRajectory Optimizer." [Repository](https://github.com/RoboticExplorationLab/Altro.jl)
- DifferentiableTrajectoryOptimization.jl. [Repository](https://github.com/lassepe/DifferentiableTrajectoryOptimization.jl)
- NASA AAS 25-290. "Q-LAW for Rapid Assessment of Low Thrust Cislunar." [NTRS](https://ntrs.nasa.gov/api/citations/20240016341/downloads/Nate_Steffen_AAS_Final_Manuscript_with_copyright.pdf)

### GPU NLP Solvers
- HiOp (LLNL): https://github.com/LLNL/hiop
- Shin, S. et al. (2024). "Accelerating Optimal Power Flow with GPUs." [arXiv:2307.16830](https://arxiv.org/pdf/2307.16830)
- MadNCL (2025). "GPU Implementation of Algorithm NCL." [arXiv:2510.05885](https://arxiv.org/html/2510.05885)

### Parallel MBH
- McCarty, S.L. & McGuire, M.L. (2018). "Parallel Monotonic Basin Hopping for Low Thrust Trajectory Optimization." [NASA NTRS](https://ntrs.nasa.gov/search.jsp?R=20180002379)

### AD Libraries
- CoDiPack: https://github.com/SciCompKL/CoDiPack ([ACM TOMS 2019](https://dl.acm.org/doi/10.1145/3356900))
- Adept 2: https://www.met.reading.ac.uk/~swrhgnrj/publications/adept.pdf
- FastAD: https://github.com/JamesYang007/FastAD
- Enzyme AD: https://enzyme.mit.edu/

### Rust Ecosystem
- burn framework: https://github.com/tracel-ai/burn
- CubeCL (GPU compute language for Rust): https://github.com/tracel-ai/cubecl
- faer (dense linear algebra): https://github.com/sarah-ek/faer-rs
- cudarc (safe CUDA bindings): https://github.com/coreylowman/cudarc
- Corrosion (CMake + Cargo): https://github.com/corrosion-rs/corrosion
- cbindgen (C/C++ header generation): https://github.com/mozilla/cbindgen

### Other
- Eigen library: https://eigen.tuxfamily.org/
- IPOPT: https://github.com/coin-or/Ipopt
- DACE (Differential Algebra): https://github.com/dacelib/dace
- Nyx Space (STM via dual numbers): [nyxspace.com](https://nyxspace.com/nyxspace/MathSpec/optimization/stm/)
- PSOPT (collocation): https://github.com/PSOPT/psopt
- ifopt (SNOPT+IPOPT unified interface): https://github.com/ethz-adrl/ifopt
- ChebTools (NIST Chebyshev library): https://github.com/usnistgov/ChebTools
