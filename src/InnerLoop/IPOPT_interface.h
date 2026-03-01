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

// header file for IPOPT interface
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
            // constructor
            IPOPT_interface() : NLP_interface::NLP_interface() {};
            IPOPT_interface(problem* myProblem,
                const NLPoptions& myOptions);

            // run NLP
            virtual void run_NLP(const bool& X0_is_scaled = true);

            // Methods called by EMTG_IPOPT_NLP to interact with the NLP_interface

            // Update the NLP chaperone incumbent if current point is better
            void update_NLP_incumbent(const std::vector<doubleType>& X_scaled,
                                      const std::vector<doubleType>& X_unscaled,
                                      const std::vector<doubleType>& F,
                                      const std::vector<double>& G,
                                      double worst_feasibility);

            // Called by finalize_solution to copy the final solution
            void set_final_solution(const std::vector<doubleType>& X_scaled,
                                    Ipopt::SolverReturn status);

            // Get the IPOPT solver return status
            Ipopt::SolverReturn getIPOPT_status() const { return this->ipopt_status; }

        private:
            // IPOPT solver return status
            Ipopt::SolverReturn ipopt_status;

        }; // end class IPOPT_interface

    } // end namespace Solvers
} // end namespace EMTG

#endif // USE_IPOPT
