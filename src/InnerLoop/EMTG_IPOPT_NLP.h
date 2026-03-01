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

// EMTG_IPOPT_NLP adapter class
// Translates between EMTG's combined F/G NLP formulation and IPOPT's
// separated objective/constraint/Jacobian interface (Ipopt::TNLP)

#pragma once

#ifdef USE_IPOPT

#include "IpTNLP.hpp"
#include "problem.h"
#include "NLPoptions.h"
#include "EMTG_math.h"

#include <vector>
#include <ctime>

namespace EMTG
{
    namespace Solvers
    {
        // forward declaration
        class IPOPT_interface;

        class EMTG_IPOPT_NLP : public Ipopt::TNLP
        {
        public:
            // constructor
            EMTG_IPOPT_NLP(IPOPT_interface* myIPOPT_interface,
                           problem* myProblem,
                           const NLPoptions& myOptions);

            // destructor
            virtual ~EMTG_IPOPT_NLP() {}

            // TNLP interface methods

            // Returns the problem dimensions and sparsity counts
            virtual bool get_nlp_info(Ipopt::Index& n,
                                      Ipopt::Index& m,
                                      Ipopt::Index& nnz_jac_g,
                                      Ipopt::Index& nnz_h_lag,
                                      IndexStyleEnum& index_style);

            // Returns variable and constraint bounds
            virtual bool get_bounds_info(Ipopt::Index n,
                                         Ipopt::Number* x_l,
                                         Ipopt::Number* x_u,
                                         Ipopt::Index m,
                                         Ipopt::Number* g_l,
                                         Ipopt::Number* g_u);

            // Returns the initial point
            virtual bool get_starting_point(Ipopt::Index n,
                                            bool init_x,
                                            Ipopt::Number* x,
                                            bool init_z,
                                            Ipopt::Number* z_L,
                                            Ipopt::Number* z_U,
                                            Ipopt::Index m,
                                            bool init_lambda,
                                            Ipopt::Number* lambda);

            // Evaluates the objective function value
            virtual bool eval_f(Ipopt::Index n,
                                const Ipopt::Number* x,
                                bool new_x,
                                Ipopt::Number& obj_value);

            // Evaluates the gradient of the objective function
            virtual bool eval_grad_f(Ipopt::Index n,
                                     const Ipopt::Number* x,
                                     bool new_x,
                                     Ipopt::Number* grad_f);

            // Evaluates the constraint values
            virtual bool eval_g(Ipopt::Index n,
                                const Ipopt::Number* x,
                                bool new_x,
                                Ipopt::Index m,
                                Ipopt::Number* g);

            // Evaluates the Jacobian of the constraints
            // Two-call pattern: when values==NULL, fill iRow/jCol with sparsity structure;
            // otherwise fill values with Jacobian entries
            virtual bool eval_jac_g(Ipopt::Index n,
                                    const Ipopt::Number* x,
                                    bool new_x,
                                    Ipopt::Index m,
                                    Ipopt::Index nele_jac,
                                    Ipopt::Index* iRow,
                                    Ipopt::Index* jCol,
                                    Ipopt::Number* values);

            // Evaluates the Hessian of the Lagrangian
            // Returns false to signal IPOPT to use L-BFGS approximation
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
                                Ipopt::Number* values);

            // Called by IPOPT after each iteration for monitoring/early termination
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
                                               Ipopt::IpoptCalculatedQuantities* ip_cq);

            // Called by IPOPT when the solver is finished
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
                                           Ipopt::IpoptCalculatedQuantities* ip_cq);

        private:
            // Helper: evaluate EMTG problem if x has changed since last evaluation
            void evaluate_if_new(const Ipopt::Number* x, bool new_x, bool needG);

            // Pointers to parent objects
            IPOPT_interface* myIPOPT_interface;
            problem* myProblem;
            NLPoptions myOptions;

            // Problem dimensions
            size_t nX;  // number of decision variables
            size_t nF;  // number of F entries (objective + constraints)
            size_t nG;  // number of nonlinear Jacobian entries
            size_t nA;  // number of linear Jacobian entries

            // Pre-partitioned index sets for objective (F[0]) entries in G
            std::vector<size_t> obj_G_indices;   // indices into G[] that correspond to objective
            std::vector<size_t> obj_G_jvar;      // column indices for objective G entries

            // Pre-partitioned index sets for constraint (F[1:]) entries in G
            std::vector<size_t> con_G_indices;   // indices into G[] that correspond to constraints
            std::vector<size_t> con_G_irow;      // row indices shifted by -1 for IPOPT
            std::vector<size_t> con_G_jvar;      // column indices for constraint G entries

            // Pre-partitioned index sets for objective entries in A (linear)
            std::vector<size_t> obj_A_indices;   // indices into A[] for objective
            std::vector<size_t> obj_A_jvar;      // column indices for objective A entries

            // Pre-partitioned index sets for constraint entries in A (linear)
            std::vector<size_t> con_A_indices;   // indices into A[] for constraints
            std::vector<size_t> con_A_irow;      // row indices shifted by -1 for IPOPT
            std::vector<size_t> con_A_jvar;      // column indices for constraint A entries

            // Total nonzeros in constraint Jacobian (con_G + con_A)
            Ipopt::Index nnz_jac;

            // Cached evaluation results
            std::vector<doubleType> cached_F;
            std::vector<double> cached_G;
            std::vector<doubleType> cached_X_unscaled;
            std::vector<doubleType> cached_X_scaled;
            bool evaluation_valid;
            bool gradient_valid;

            // Constraint bounds (from F[1:nF-1])
            std::vector<double> constraint_lowerbounds;
            std::vector<double> constraint_upperbounds;

            // NLP start time for time limit checking
            time_t NLP_start_time;

        }; // end class EMTG_IPOPT_NLP
    } // end namespace Solvers
} // end namespace EMTG

#endif // USE_IPOPT
