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

// IPOPT interface implementation

#ifdef USE_IPOPT

#include "IPOPT_interface.h"
#include "EMTG_IPOPT_NLP.h"
#include "EMTG_math.h"

#include <iostream>
#include <cmath>
#include <ctime>

namespace EMTG
{
    namespace Solvers
    {
        IPOPT_interface::IPOPT_interface(problem* myProblem,
            const NLPoptions& myOptions) :
            NLP_interface::NLP_interface(myProblem, myOptions),
            ipopt_status(Ipopt::SolverReturn::INTERNAL_ERROR)
        {
            // The NLP_interface base constructor has already set up:
            // nX, nF, nG, nA, X0_scaled, X0_unscaled, X_scaled, X_unscaled,
            // F, G, Xlowerbounds, Xupperbounds, Flowerbounds, Fupperbounds,
            // iGfun, jGvar, iAfun, jAvar, chaperone vectors, etc.

            // Set first_feasibility flag
            this->first_feasibility = false;
        }

        void IPOPT_interface::run_NLP(const bool& X0_is_scaled)
        {
            // Step 1: Scale/unscale X0
            if (!X0_is_scaled)
                this->scaleX0();
            else
                this->unscaleX0();

            this->X_scaled = this->X0_scaled;

            // Step 2: Evaluate initial guess to make sure it works
            this->myProblem->evaluate(this->X0_unscaled, this->F, this->myProblem->G, false);

            // Step 3: Set variable bounds (same pattern as SNOPT_interface)
            for (size_t Xindex = 0; Xindex < this->nX; ++Xindex)
            {
                this->Xupperbounds[Xindex] = (this->myProblem->Xupperbounds[Xindex] - this->myProblem->Xlowerbounds[Xindex])
                                            / this->myProblem->X_scale_factors[Xindex];
                this->Xlowerbounds[Xindex] = 0.0;
            }

            // Step 4: Initialize chaperone incumbent
            this->NLP_start_time = time(NULL);
            this->mostRecentNLPWriteTime = time(NULL);
            this->newBestIncumbent = false;
            this->feasibility_metric_NLP_incumbent = 1.0e+101;
            this->J_NLP_incumbent = math::LARGE;
            this->movie_frame_count = 0;
            this->first_feasibility = false;

            // Step 5: Create IPOPT application and set options
            Ipopt::SmartPtr<Ipopt::IpoptApplication> app = IpoptApplicationFactory();

            // Must use L-BFGS since we don't provide Hessian
            app->Options()->SetStringValue("hessian_approximation", "limited-memory");

            // Set iteration limit: use IPOPT-specific if set (not -1), else fall back to general
            int ipopt_max_iter = this->myOptions.get_ipopt_max_iterations();
            if (ipopt_max_iter > 0)
            {
                app->Options()->SetIntegerValue("max_iter", ipopt_max_iter);
            }
            else
            {
                app->Options()->SetIntegerValue("max_iter", static_cast<int>(this->myOptions.get_major_iterations_limit()));
            }

            // Set convergence tolerance: use IPOPT-specific if set (> 0), else fall back to general
            double ipopt_tol = this->myOptions.get_ipopt_convergence_tolerance();
            if (ipopt_tol > 0.0)
            {
                app->Options()->SetNumericValue("tol", ipopt_tol);
            }
            else
            {
                app->Options()->SetNumericValue("tol", this->myOptions.get_optimality_tolerance());
            }

            // Set constraint violation tolerance
            double ipopt_constr_tol = this->myOptions.get_ipopt_constraint_violation_tolerance();
            if (ipopt_constr_tol > 0.0)
            {
                app->Options()->SetNumericValue("constr_viol_tol", ipopt_constr_tol);
            }
            else
            {
                app->Options()->SetNumericValue("constr_viol_tol", this->myOptions.get_feasibility_tolerance());
            }

            // Set max wall time
            int ipopt_max_time = this->myOptions.get_ipopt_max_run_time();
            if (ipopt_max_time > 0)
            {
                app->Options()->SetNumericValue("max_wall_time", static_cast<double>(ipopt_max_time));
            }
            else
            {
                app->Options()->SetNumericValue("max_wall_time", static_cast<double>(this->myOptions.get_max_run_time_seconds()));
            }

            // Set barrier parameter strategy
            int mu_strategy = this->myOptions.get_ipopt_mu_strategy();
            if (mu_strategy == 1)
            {
                app->Options()->SetStringValue("mu_strategy", "adaptive");
            }
            else
            {
                app->Options()->SetStringValue("mu_strategy", "monotone");
            }

            // Set print level
            int print_level = this->myOptions.get_ipopt_print_level();
            if (print_level >= 0)
            {
                app->Options()->SetIntegerValue("print_level", print_level);
            }
            else if (this->myOptions.get_quiet_NLP())
            {
                app->Options()->SetIntegerValue("print_level", 0);
            }
            else
            {
                app->Options()->SetIntegerValue("print_level", 5); // IPOPT default
            }

            // Set output file if specified
            std::string output_file_path = this->myOptions.get_output_file_path();
            if (output_file_path != "")
            {
                app->Options()->SetStringValue("output_file", output_file_path);
            }

            // Derivative checking if requested
            if (this->myOptions.get_check_derivatives())
            {
                app->Options()->SetStringValue("derivative_test", "first-order");
                app->Options()->SetNumericValue("derivative_test_tol", 1.0e-4);
            }

            // Step 6: Initialize IPOPT application
            Ipopt::ApplicationReturnStatus init_status = app->Initialize();
            if (init_status != Ipopt::Solve_Succeeded)
            {
                std::cout << "IPOPT application initialization failed with status " << init_status << std::endl;
                return;
            }

            // Step 7: Create the EMTG_IPOPT_NLP adapter and solve
            Ipopt::SmartPtr<Ipopt::TNLP> myNLP = new EMTG_IPOPT_NLP(this, this->myProblem, this->myOptions);

            Ipopt::ApplicationReturnStatus solve_status = app->OptimizeTNLP(myNLP);

            // Step 8: Post-solve processing

            // Unscale the solution
            this->unscaleX();

            // Perform a feasibility check on the exit point
            this->myProblem->check_feasibility(this->X_unscaled,
                this->F,
                this->worst_decision_variable,
                this->worst_constraint,
                this->feasibility_metric,
                this->normalized_feasibility_metric,
                this->distance_from_equality_filament,
                this->decision_vector_feasibility_metric);

            double worst_feasibility = fmax(this->normalized_feasibility_metric, this->decision_vector_feasibility_metric);

            // Adopt the chaperone incumbent if appropriate
            if (this->myOptions.get_enable_NLP_chaperone())
            {
                this->unscaleX_NLP_incumbent();

                if (worst_feasibility < this->myOptions.get_feasibility_tolerance()
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    // Both incumbent point and NLP exit point are feasible
                    if (this->J_NLP_incumbent < this->F.front())
                    {
                        this->X_unscaled = this->X_NLP_incumbent_unscaled;
                        this->X_scaled = this->X_NLP_incumbent_scaled;
                        this->F = this->F_NLP_incumbent;

                        std::cout << "NLP incumbent point and exit point are feasible and incumbent point is superior to exit point." << std::endl;
                    }
                    else
                    {
                        std::cout << "NLP incumbent point and exit point are feasible and exit point is superior to incumbent point." << std::endl;
                    }
                }
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    // Incumbent point is feasible but exit point is not
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;

                    std::cout << "NLP incumbent point was feasible and the exit point was not." << std::endl;
                }
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility)
                {
                    // Incumbent point is infeasible but less infeasible than exit point
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;

                    std::cout << "NLP incumbent point was infeasible but less infeasible than the exit point." << std::endl;
                }
                else
                {
                    std::cout << "NLP exit point was superior to incumbent point." << std::endl;
                }
            } // end NLP chaperone

            // Report solver status
            if (!this->myOptions.get_quiet_NLP())
            {
                std::cout << "IPOPT finished with status: " << solve_status << std::endl;
            }
        }

        void IPOPT_interface::update_NLP_incumbent(const std::vector<doubleType>& X_scaled_in,
                                                    const std::vector<doubleType>& X_unscaled_in,
                                                    const std::vector<doubleType>& F_in,
                                                    const std::vector<double>& G_in,
                                                    double worst_feasibility)
        {
            // If the current point is feasible and the incumbent is also feasible
            if (worst_feasibility < this->myOptions.get_feasibility_tolerance()
                && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
            {
                // Both are feasible: update if current is more optimal
                if (F_in.front() < this->J_NLP_incumbent)
                {
                    this->J_NLP_incumbent = F_in.front();
                    this->feasibility_metric_NLP_incumbent = worst_feasibility;
                    this->X_NLP_incumbent_unscaled = X_unscaled_in;
                    this->X_NLP_incumbent_scaled = X_scaled_in;
                    this->F_NLP_incumbent = F_in;

                    // Check if this is a new global best
                    if (this->J_NLP_incumbent < this->JGlobalIncumbent)
                    {
                        this->newBestIncumbent = true;
                        this->JGlobalIncumbent = this->J_NLP_incumbent;
                    }
                }

                // Check first feasibility (should not normally trigger here, but be safe)
                if (this->first_feasibility == false)
                {
                    this->first_feasibility = true;

                    if (this->J_NLP_incumbent < this->JGlobalIncumbent)
                    {
                        this->newBestIncumbent = true;
                        this->JGlobalIncumbent = this->J_NLP_incumbent;

                        this->myProblem->Xopt = this->X_NLP_incumbent_unscaled;
                        this->myProblem->F = this->F_NLP_incumbent;
                        this->myProblem->evaluate(this->X_NLP_incumbent_unscaled, this->F_NLP_incumbent, this->G, false);

                        this->myProblem->what_the_heck_am_I_called(SolutionOutputType::SUCCESS);
                        this->myProblem->output(this->myProblem->options.outputfile);
                        this->mostRecentNLPWriteTime = time(NULL);
                        this->newBestIncumbent = false;
                    }
                }
            }
            // If the current point is more feasible than the incumbent
            else if (worst_feasibility < this->feasibility_metric_NLP_incumbent)
            {
                this->J_NLP_incumbent = F_in.front();
                this->feasibility_metric_NLP_incumbent = worst_feasibility;
                this->X_NLP_incumbent_unscaled = X_unscaled_in;
                this->X_NLP_incumbent_scaled = X_scaled_in;
                this->F_NLP_incumbent = F_in;

                // If the current point is feasible and this is the first feasible point
                if (worst_feasibility < this->myOptions.get_feasibility_tolerance()
                    && this->first_feasibility == false)
                {
                    this->first_feasibility = true;

                    if (this->J_NLP_incumbent < this->JGlobalIncumbent)
                    {
                        this->newBestIncumbent = true;
                        this->JGlobalIncumbent = this->J_NLP_incumbent;

                        this->myProblem->Xopt = this->X_NLP_incumbent_unscaled;
                        this->myProblem->F = this->F_NLP_incumbent;
                        this->myProblem->evaluate(this->X_NLP_incumbent_unscaled, this->F_NLP_incumbent, this->G, false);

                        this->myProblem->what_the_heck_am_I_called(SolutionOutputType::SUCCESS);
                        this->myProblem->output(this->myProblem->options.outputfile);
                        this->mostRecentNLPWriteTime = time(NULL);
                        this->newBestIncumbent = false;
                    }
                }
            }

            // Periodic write check: write intermediate solution if enough time has passed
            time_t now = time(NULL);
            if ((now - this->mostRecentNLPWriteTime) > this->myProblem->options.NLP_write_output_check_time
                && this->newBestIncumbent)
            {
                this->myProblem->Xopt = this->X_NLP_incumbent_unscaled;
                this->myProblem->F = this->F_NLP_incumbent;
                this->myProblem->evaluate(this->X_NLP_incumbent_unscaled, this->F_NLP_incumbent, this->G, false);
                this->myProblem->what_the_heck_am_I_called(SolutionOutputType::SUCCESS);
                this->myProblem->output(this->myProblem->options.outputfile);

                if (!this->myOptions.get_quiet_NLP())
                {
                    std::cout << "Intermediate NLP solution written to file with new best J = "
                              << this->JGlobalIncumbent _GETVALUE << "." << std::endl;
                }

                this->mostRecentNLPWriteTime = time(NULL);
                this->newBestIncumbent = false;
            }
        }

        void IPOPT_interface::set_final_solution(const std::vector<doubleType>& X_scaled_in,
                                                  Ipopt::SolverReturn status)
        {
            // Copy the final scaled solution into NLP_interface's X_scaled
            this->X_scaled = X_scaled_in;

            // Store the IPOPT status
            this->ipopt_status = status;

            // Re-evaluate at the final point to populate F and G
            this->unscaleX();
            this->myProblem->evaluate(this->X_unscaled, this->F, this->G, false);
        }

    } // end namespace Solvers
} // end namespace EMTG

#endif // USE_IPOPT
