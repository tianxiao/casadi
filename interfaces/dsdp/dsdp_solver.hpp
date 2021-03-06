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

#ifndef DSDP_SOLVER_HPP
#define DSDP_SOLVER_HPP

#include "symbolic/fx/sdp_solver.hpp"


namespace CasADi {
  
  
// Forward declaration of internal class 
class DSDPInternal;

  /** \brief Interface to DSDP Solver for semi definite programming

   @copydoc SDPSolver_doc
   
   Warning: The solver DSDP breaks down when you try to achieve linear equality (Sum a_i x_i - c) == 0
     by decomposing into two sets of inequalities:
       (Sum a_i x_i - c) >= 0
       (Sum -a_i x_i - (-c)) >= 0
     
     You need to manually leave a gap between these eaualities:
       (Sum a_i x_i - c) >= 0
       (Sum -a_i x_i - (-c-eps)) >= 0
      
      with eps = 1e-6 flor example.
       
      
   \author Joris Gillis
   \date 2013

  */
class DSDPSolver : public SDPSolver {
public:

  /** \brief  Default constructor */
  DSDPSolver();
  
  explicit DSDPSolver(const CRSSparsity &C, const CRSSparsity &A);
  
  /** \brief  Access functions of the node */
  DSDPInternal* operator->();
  const DSDPInternal* operator->() const;

  /// Check if the node is pointing to the right type of object
  virtual bool checkNode() const;
  
  /// Static creator function
  #ifdef SWIG
  %callback("%s_cb");
  #endif
  static SDPSolver creator(const CRSSparsity &C, const CRSSparsity &A){ return DSDPSolver(C,A);}
  #ifdef SWIG
  %nocallback;
  #endif

};


} // namespace CasADi

#endif //DSDP_SOLVER_HPP

