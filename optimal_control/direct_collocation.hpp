/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef DIRECT_COLLOCATION_HPP
#define DIRECT_COLLOCATION_HPP

#include "../symbolic/fx/ocp_solver.hpp"
#include "../symbolic/fx/nlp_solver.hpp"

namespace CasADi{
  class DirectCollocationInternal;
    
  /** \brief Direct collocation
   *
   *   \author Joel Andersson
   *   \date 2012
  */ 
class DirectCollocation : public OCPSolver{
  public:
    /// Default constructor
    DirectCollocation();
  
    /// Constructor
    explicit DirectCollocation(const FX& ffcn, const FX& mfcn, const FX& cfcn=FX(), const FX& rfcn=FX());

    /// Access functions of the node
    DirectCollocationInternal* operator->();

    /// Const access functions of the node
    const DirectCollocationInternal* operator->() const;
    
    /// Get the variables
    void getGuess(std::vector<double>& V_init) const;
    
    /// Get the variables
    void getVariableBounds(std::vector<double>& V_min, std::vector<double>& V_max) const;
    
    /// Get the constraints
    void getConstraintBounds(std::vector<double>& G_min, std::vector<double>& G_max) const;

    /// Set the optimal solution
    void setOptimalSolution( const std::vector<double> &V_opt );
    
    /// Access the underlying NLPSolver object
    NLPSolver getNLPSolver() const;

    /// Prints out a human readable report about possible constraint violations, after solving 
    void reportConstraints(std::ostream &stream=std::cout);

    /// Return the report as a string
    std::string getReportConstraints() { std::stringstream s; reportConstraints(s); return s.str(); }
};
                        
} // namespace CasADi

#endif // DIRECT_COLLOCATION_HPP
