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

#include "nlp_solver.hpp"
#include "nlp_solver_internal.hpp"

namespace CasADi{

NLPSolver::NLPSolver(){
}

NLPSolverInternal* NLPSolver::operator->(){
  return (NLPSolverInternal*)(FX::operator->());
}

const NLPSolverInternal* NLPSolver::operator->() const{
  return (const NLPSolverInternal*)(FX::operator->());
}
    
bool NLPSolver::checkNode() const{
  return dynamic_cast<const NLPSolverInternal*>(get())!=0;
}

void NLPSolver::reportConstraints(std::ostream &stream) { 
  (*this)->reportConstraints();
}

void NLPSolver::setQPOptions() {
  (*this)->setQPOptions();
}

FX NLPSolver::getF() const { return isNull()? FX() : dynamic_cast<const NLPSolverInternal*>(get())->F_; }
  
FX NLPSolver::getG() const { return isNull()? FX() : dynamic_cast<const NLPSolverInternal*>(get())->G_; }

FX NLPSolver::getH() const { return isNull()? FX() : dynamic_cast<const NLPSolverInternal*>(get())->H_; }
  
FX NLPSolver::getJ() const { return isNull()? FX() : dynamic_cast<const NLPSolverInternal*>(get())->J_; }


} // namespace CasADi

