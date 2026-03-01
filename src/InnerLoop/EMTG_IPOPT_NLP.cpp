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

// EMTG_IPOPT_NLP adapter implementation
// Translates between EMTG's combined F/G NLP formulation and IPOPT's
// separated objective/constraint/Jacobian interface

#ifdef USE_IPOPT

#include "EMTG_IPOPT_NLP.h"
#include "IPOPT_interface.h"
#include "EMTG_math.h"

#include <iostream>
#include <cmath>
#include <ctime>

namespace EMTG
{
    namespace Solvers
    {
        EMTG_IPOPT_NLP::EMTG_IPOPT_NLP(IPOPT_interface* myIPOPT_interface,
                                         problem* myProblem,
                                         const NLPoptions& myOptions) :
            myIPOPT_interface(myIPOPT_interface),
            myProblem(myProblem),
            myOptions(myOptions),
            evaluation_valid(false),
            gradient_valid(false)
        {
            // Get problem dimensions from the NLP_interface (which has already
            // been set up by the base class constructor)
            this->nX = myIPOPT_interface->getnX();
            this->nF = myIPOPT_interface->getnF();
            this->nG = myIPOPT_interface->getnG();
            this->nA = myProblem->Adescriptions.size();

            // Get the sparsity pattern from the NLP_interface
            std::vector<size_t> iGfun = myIPOPT_interface->getiGfun();
            std::vector<size_t> jGvar = myIPOPT_interface->getjGvar();

            // Pre-partition G entries into objective vs constraint sets
            for (size_t Gindex = 0; Gindex < this->nG; ++Gindex)
            {
                if (iGfun[Gindex] == 0)
                {
                    // This G entry affects the objective function (F[0])
                    this->obj_G_indices.push_back(Gindex);
                    this->obj_G_jvar.push_back(jGvar[Gindex]);
                }
                else
                {
                    // This G entry affects a constraint (F[1:])
                    this->con_G_indices.push_back(Gindex);
                    this->con_G_irow.push_back(iGfun[Gindex] - 1); // shift row index by -1 for IPOPT
                    this->con_G_jvar.push_back(jGvar[Gindex]);
                }
            }

            // Pre-partition A (linear) entries into objective vs constraint sets
            for (size_t Aindex = 0; Aindex < this->nA; ++Aindex)
            {
                if (myProblem->iAfun[Aindex] == 0)
                {
                    // This A entry affects the objective function
                    this->obj_A_indices.push_back(Aindex);
                    this->obj_A_jvar.push_back(myProblem->jAvar[Aindex]);
                }
                else
                {
                    // This A entry affects a constraint
                    this->con_A_indices.push_back(Aindex);
                    this->con_A_irow.push_back(myProblem->iAfun[Aindex] - 1);
                    this->con_A_jvar.push_back(myProblem->jAvar[Aindex]);
                }
            }

            // Total nonzeros in constraint Jacobian
            this->nnz_jac = static_cast<Ipopt::Index>(this->con_G_indices.size() + this->con_A_indices.size());

            // Copy constraint bounds from F bounds (skip F[0] which is the objective)
            std::vector<double> Flowerbounds = myIPOPT_interface->getFlowerbounds();
            std::vector<double> Fupperbounds = myIPOPT_interface->getFupperbounds();
            this->constraint_lowerbounds.resize(this->nF - 1);
            this->constraint_upperbounds.resize(this->nF - 1);
            for (size_t Findex = 1; Findex < this->nF; ++Findex)
            {
                this->constraint_lowerbounds[Findex - 1] = Flowerbounds[Findex];
                this->constraint_upperbounds[Findex - 1] = Fupperbounds[Findex];
            }

            // Allocate cached evaluation storage
            this->cached_F.resize(this->nF, 0.0);
            this->cached_G.resize(this->nG, 0.0);
            this->cached_X_unscaled.resize(this->nX, 0.0);
            this->cached_X_scaled.resize(this->nX, 0.0);

            // Record NLP start time
            this->NLP_start_time = time(NULL);
        }

        //***********************************************
        // TNLP interface implementations
        //***********************************************

        bool EMTG_IPOPT_NLP::get_nlp_info(Ipopt::Index& n,
                                            Ipopt::Index& m,
                                            Ipopt::Index& nnz_jac_g,
                                            Ipopt::Index& nnz_h_lag,
                                            IndexStyleEnum& index_style)
        {
            n = static_cast<Ipopt::Index>(this->nX);
            m = static_cast<Ipopt::Index>(this->nF - 1); // constraints only, no objective
            nnz_jac_g = this->nnz_jac;
            nnz_h_lag = 0; // we use L-BFGS Hessian approximation
            index_style = Ipopt::TNLP::C_STYLE; // 0-based indexing

            return true;
        }

        bool EMTG_IPOPT_NLP::get_bounds_info(Ipopt::Index n,
                                               Ipopt::Number* x_l,
                                               Ipopt::Number* x_u,
                                               Ipopt::Index m,
                                               Ipopt::Number* g_l,
                                               Ipopt::Number* g_u)
        {
            // Variable bounds: EMTG scales X to [0, (Xupper-Xlower)/X_scale_factor]
            // The NLP_interface base constructor sets Xlowerbounds=0 and
            // Xupperbounds = (Xupper - Xlower) / X_scale_factor
            // But SNOPT_interface::run_NLP recalculates them.
            // We use the same pattern as SNOPT: scale to [0, (Xupper-Xlower)/scale]
            for (Ipopt::Index i = 0; i < n; ++i)
            {
                x_l[i] = 0.0;
                x_u[i] = (this->myProblem->Xupperbounds[i] - this->myProblem->Xlowerbounds[i])
                        / this->myProblem->X_scale_factors[i];
            }

            // Constraint bounds (F[1:nF-1])
            for (Ipopt::Index i = 0; i < m; ++i)
            {
                g_l[i] = this->constraint_lowerbounds[i];
                g_u[i] = this->constraint_upperbounds[i];
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::get_starting_point(Ipopt::Index n,
                                                  bool init_x,
                                                  Ipopt::Number* x,
                                                  bool init_z,
                                                  Ipopt::Number* z_L,
                                                  Ipopt::Number* z_U,
                                                  Ipopt::Index m,
                                                  bool init_lambda,
                                                  Ipopt::Number* lambda)
        {
            // We only provide initial x, not dual variables
            if (init_x)
            {
                std::vector<doubleType> X0_scaled = this->myIPOPT_interface->getX0_scaled();
                for (Ipopt::Index i = 0; i < n; ++i)
                {
                    x[i] = X0_scaled[i] _GETVALUE;
                }
            }

            // We do not initialize dual variables
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
                {
                    lambda[i] = 0.0;
                }
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_f(Ipopt::Index n,
                                      const Ipopt::Number* x,
                                      bool new_x,
                                      Ipopt::Number& obj_value)
        {
            // Evaluate the problem if x has changed
            this->evaluate_if_new(x, new_x, false);

            // In FeasiblePoint mode, objective is always 0
            if (this->myOptions.get_SolverMode() == NLPMode::FeasiblePoint)
            {
                obj_value = 0.0;
            }
            else
            {
                obj_value = this->cached_F[0] _GETVALUE;
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_grad_f(Ipopt::Index n,
                                           const Ipopt::Number* x,
                                           bool new_x,
                                           Ipopt::Number* grad_f)
        {
            // Evaluate with gradients
            this->evaluate_if_new(x, new_x, true);

            // In FeasiblePoint mode, gradient is all zeros
            if (this->myOptions.get_SolverMode() == NLPMode::FeasiblePoint)
            {
                for (Ipopt::Index i = 0; i < n; ++i)
                {
                    grad_f[i] = 0.0;
                }
                return true;
            }

            // Initialize dense gradient to zero
            for (Ipopt::Index i = 0; i < n; ++i)
            {
                grad_f[i] = 0.0;
            }

            // Accumulate nonlinear objective gradient entries from G
            // EMTG stores unscaled Jacobian; IPOPT sees scaled X, so
            // dF/dx_scaled = dF/dx_unscaled * dx_unscaled/dx_scaled = G[k] * X_scale_factors[j]
            for (size_t k = 0; k < this->obj_G_indices.size(); ++k)
            {
                size_t Gindex = this->obj_G_indices[k];
                size_t j = this->obj_G_jvar[k];
                grad_f[j] += this->cached_G[Gindex] * this->myProblem->X_scale_factors[j];
            }

            // Accumulate linear objective gradient entries from A
            for (size_t k = 0; k < this->obj_A_indices.size(); ++k)
            {
                size_t Aindex = this->obj_A_indices[k];
                size_t j = this->obj_A_jvar[k];
                grad_f[j] += this->myProblem->A[Aindex] * this->myProblem->X_scale_factors[j];
            }

            return true;
        }

        bool EMTG_IPOPT_NLP::eval_g(Ipopt::Index n,
                                      const Ipopt::Number* x,
                                      bool new_x,
                                      Ipopt::Index m,
                                      Ipopt::Number* g)
        {
            // Evaluate the problem if x has changed
            this->evaluate_if_new(x, new_x, false);

            // Copy constraint values from F[1:nF-1]
            for (Ipopt::Index i = 0; i < m; ++i)
            {
                g[i] = this->cached_F[i + 1] _GETVALUE;
            }

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
                // Structure call: fill in sparsity pattern (iRow, jCol)
                Ipopt::Index idx = 0;

                // Nonlinear constraint entries from G
                for (size_t k = 0; k < this->con_G_indices.size(); ++k)
                {
                    iRow[idx] = static_cast<Ipopt::Index>(this->con_G_irow[k]);
                    jCol[idx] = static_cast<Ipopt::Index>(this->con_G_jvar[k]);
                    ++idx;
                }

                // Linear constraint entries from A
                for (size_t k = 0; k < this->con_A_indices.size(); ++k)
                {
                    iRow[idx] = static_cast<Ipopt::Index>(this->con_A_irow[k]);
                    jCol[idx] = static_cast<Ipopt::Index>(this->con_A_jvar[k]);
                    ++idx;
                }
            }
            else
            {
                // Values call: fill in Jacobian values
                this->evaluate_if_new(x, new_x, true);

                Ipopt::Index idx = 0;

                // Nonlinear constraint Jacobian entries from G, scaled
                for (size_t k = 0; k < this->con_G_indices.size(); ++k)
                {
                    size_t Gindex = this->con_G_indices[k];
                    size_t j = this->con_G_jvar[k];
                    values[idx] = this->cached_G[Gindex] * this->myProblem->X_scale_factors[j];
                    ++idx;
                }

                // Linear constraint Jacobian entries from A, scaled
                for (size_t k = 0; k < this->con_A_indices.size(); ++k)
                {
                    size_t Aindex = this->con_A_indices[k];
                    size_t j = this->con_A_jvar[k];
                    values[idx] = this->myProblem->A[Aindex] * this->myProblem->X_scale_factors[j];
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
            // Return false to signal IPOPT to use L-BFGS Hessian approximation
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
            // NLP chaperone logic
            if (this->myOptions.get_enable_NLP_chaperone() && this->evaluation_valid)
            {
                // Compute current feasibility
                double f_current;
                double f_abs_current;
                double distance_from_equality_filament;
                size_t worst_constraint;
                size_t worst_decision_variable;
                double decision_variable_feasibility_metric;

                try
                {
                    this->myProblem->check_feasibility(this->cached_X_unscaled,
                        this->cached_F,
                        worst_decision_variable,
                        worst_constraint,
                        f_abs_current,
                        f_current,
                        distance_from_equality_filament,
                        decision_variable_feasibility_metric,
                        true);
                }
                catch (std::runtime_error& runtime_error)
                {
                    if (!this->myOptions.get_quiet_NLP())
                    {
                        std::cout << runtime_error.what() << std::endl;
                    }
                    return false; // tell IPOPT to abort
                }

                double worst_feasibility = fmax(f_current, decision_variable_feasibility_metric);

                // Goal attainment check
                if (this->myOptions.get_stop_on_goal_attain())
                {
                    if (f_current < this->myOptions.get_feasibility_tolerance()
                        && this->myProblem->getUnscaledObjective() _GETVALUE < this->myOptions.get_objective_goal())
                    {
                        if (!this->myOptions.get_quiet_NLP())
                            std::cout << "NLP goal satisfied, exiting NLP" << std::endl;
                        return false; // tell IPOPT to stop
                    }
                }

                // Update chaperone incumbent
                this->myIPOPT_interface->update_NLP_incumbent(
                    this->cached_X_scaled,
                    this->cached_X_unscaled,
                    this->cached_F,
                    this->cached_G,
                    worst_feasibility);
            }

            // Check the time limit
            time_t now = time(NULL);
            if (now - this->NLP_start_time > this->myOptions.get_max_run_time_seconds())
            {
                if (!this->myOptions.get_quiet_NLP())
                    std::cout << "Exceeded NLP time limit of " << this->myOptions.get_max_run_time_seconds()
                              << " seconds. Aborting NLP run." << std::endl;
                return false; // tell IPOPT to stop
            }

            // Print movie frames if requested
            if (this->myOptions.get_print_NLP_movie_frames() && this->evaluation_valid)
            {
                this->myProblem->X = this->cached_X_unscaled;
                this->myProblem->F = this->cached_F;
                this->myProblem->output_problem_bounds_and_descriptions(
                    this->myProblem->options.working_directory + "//"
                    + "NLP_frame_" + std::to_string(iter) + ".csv");
            }

            return true; // continue optimization
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
            // Copy final solution back to IPOPT_interface
            std::vector<doubleType> X_final_scaled(this->nX);
            for (size_t i = 0; i < this->nX; ++i)
            {
                X_final_scaled[i] = x[i];
            }

            this->myIPOPT_interface->set_final_solution(X_final_scaled, status);
        }

        //***********************************************
        // Helper: evaluate EMTG problem if x has changed
        //***********************************************
        void EMTG_IPOPT_NLP::evaluate_if_new(const Ipopt::Number* x,
                                               bool new_x,
                                               bool needG)
        {
            // If IPOPT says x is new, or we haven't done a valid evaluation yet,
            // or we need gradients but haven't computed them
            if (new_x || !this->evaluation_valid || (needG && !this->gradient_valid))
            {
                // Copy and unscale x
                // x_scaled is in [0, (Xupper-Xlower)/scale]
                // x_unscaled = x_scaled * X_scale_factor + Xlower
                for (size_t i = 0; i < this->nX; ++i)
                {
                    this->cached_X_scaled[i] = x[i];
                    this->cached_X_unscaled[i] = x[i] * this->myProblem->X_scale_factors[i]
                                                + this->myProblem->Xlowerbounds[i];
                }

                // Call EMTG's combined evaluate function
                try
                {
                    this->myProblem->evaluate(this->cached_X_unscaled,
                                              this->cached_F,
                                              this->cached_G,
                                              needG);
                }
                catch (std::runtime_error& runtime_error)
                {
                    if (!this->myOptions.get_quiet_NLP())
                    {
                        std::cout << runtime_error.what() << std::endl;
                    }
                    // Set F to large values to discourage this point
                    for (size_t i = 0; i < this->nF; ++i)
                    {
                        this->cached_F[i] = math::LARGE;
                    }
                }

                this->evaluation_valid = true;
                if (needG)
                    this->gradient_valid = true;
                else
                    this->gradient_valid = false;
            }
        }

    } // end namespace Solvers
} // end namespace EMTG

#endif // USE_IPOPT
