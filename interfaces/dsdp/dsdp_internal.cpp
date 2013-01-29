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

#include "dsdp_internal.hpp"

#include "../../symbolic/stl_vector_tools.hpp"
#include "../../symbolic/matrix/matrix_tools.hpp"

/**
Some implementation details

"Multiple cones can be created for the same solver, but it is usually more efficient to group all blocks into the same conic structure." user manual

*/
using namespace std;
namespace CasADi {

DSDPInternal* DSDPInternal::clone() const{
  // Return a deep copy
  DSDPInternal* node = new DSDPInternal(input(SDP_C).sparsity(),input(SDP_A).sparsity());
  if(!node->is_init_)
    node->init();
  return node;
}
  
DSDPInternal::DSDPInternal(const CRSSparsity &C, const CRSSparsity &A) : SDPSolverInternal(C,A){
 
  casadi_assert_message(double(n_)*(double(n_)+1)/2 < std::numeric_limits<int>::max(),"Your problem size n is too large to be handled by DSDP.");

  addOption("gapTol",OT_REAL,1e-8,"Convergence criterion based on distance between primal and dual objective");
  addOption("maxIter",OT_INTEGER,500,"Maximum number of iterations");
  addOption("dualTol",OT_REAL,1e-4,"Tolerance for dual infeasibility (translates to primal infeasibility in dsdp terms)");
  addOption("primalTol",OT_REAL,1e-4,"Tolerance for primal infeasibility (translates to dual infeasibility in dsdp terms)");
  addOption("stepTol",OT_REAL,5e-2,"Terminate the solver if the step length in the primal is below this tolerance. ");
  
  // Set DSDP memory blocks to null
  dsdp_ = 0;
  sdpcone_ = 0;
}

DSDPInternal::~DSDPInternal(){ 
  if(dsdp_!=0){
    DSDPDestroy(dsdp_);
    dsdp_ = 0;
  }
}

void DSDPInternal::init(){
  // Initialize the base classes
  SDPSolverInternal::init();

  terminationReason_[DSDP_CONVERGED]="DSDP_CONVERGED";
  terminationReason_[DSDP_MAX_IT]="DSDP_MAX_IT";
  terminationReason_[DSDP_INFEASIBLE_START]="DSDP_INFEASIBLE_START";
  terminationReason_[DSDP_INDEFINITE_SCHUR_MATRIX]="DSDP_INDEFINITE SCHUR";
  terminationReason_[DSDP_SMALL_STEPS]="DSDP_SMALL_STEPS";
  terminationReason_[DSDP_NUMERICAL_ERROR]="DSDP_NUMERICAL_ERROR";
  terminationReason_[DSDP_UPPERBOUND]="DSDP_UPPERBOUND";
  terminationReason_[DSDP_USER_TERMINATION]="DSDP_USER_TERMINATION";
  terminationReason_[CONTINUE_ITERATING]="CONTINUE_ITERATING";
  
  solutionType_[DSDP_PDFEASIBLE] = "DSDP_PDFEASIBLE";
  solutionType_[DSDP_UNBOUNDED] = "DSDP_UNBOUNDED";
  solutionType_[DSDP_INFEASIBLE] = "DSDP_INFEASIBLE";
  solutionType_[DSDP_PDUNKNOWN] = "DSDP_PDUNKNOWN";
  
  // A return flag used by DSDP
  int info;
  
  // Destroy existing DSDP instance if already allocated
  if(dsdp_!=0){
    DSDPDestroy(dsdp_);
    dsdp_ = 0;
  }

  // Allocate DSDP solver memory
  info = DSDPCreate(m_, &dsdp_);
  DSDPSetStandardMonitor(dsdp_, 1);
  DSDPSetGapTolerance(dsdp_, getOption("gapTol"));
  DSDPSetMaxIts(dsdp_, getOption("maxIter"));
  DSDPSetPTolerance(dsdp_,getOption("dualTol"));
  DSDPSetRTolerance(dsdp_,getOption("primalTol"));
  DSDPSetStepTolerance(dsdp_,getOption("stepTol"));
  
  info = DSDPCreateSDPCone(dsdp_,nb_,&sdpcone_);
  for (int j=0;j<nb_;++j) {
    info = SDPConeSetBlockSize(sdpcone_, j, block_sizes_[j]);
    info = SDPConeSetSparsity(sdpcone_, j, block_sizes_[j]);
  }
  

  // Fill the data structures that hold DSDP-style sparse symmetric matrix
  pattern_.resize(m_+1);
  values_.resize(m_+1);
  
  for (int i=0;i<m_+1;++i) {
    pattern_[i].resize(nb_);
    values_[i].resize(nb_);
    for (int j=0;j<nb_;++j) {
      CRSSparsity CAij = mapping_.output(i*nb_+j).sparsity();
      pattern_[i][j].resize(CAij.sizeL());
      values_[i][j].resize(pattern_[i][j].size());
      int nz=0;
      vector<int> rowind,col;
      CAij.getSparsityCRS(rowind,col);
      for(int r=0; r<rowind.size()-1; ++r) {
        for(int el=rowind[r]; el<rowind[r+1]; ++el){
         if(r>=col[el]){
           pattern_[i][j][nz++] = r*(r + 1)/2 + col[el];
         }
        }
      }
      mapping_.output(i*nb_+j).get(values_[i][j],SPARSESYM);
    }
  }
  
  if (calc_dual_) {
    store_X_.resize(nb_);
    for (int j=0;j<nb_;++j) {
      store_X_[j].resize(block_sizes_[j]*(block_sizes_[j]+1)/2);
    }
  }
  if (calc_p_) {
    store_P_.resize(nb_);
    for (int j=0;j<nb_;++j) {
      store_P_[j].resize(block_sizes_[j]*(block_sizes_[j]+1)/2);
    }
  }
}

void DSDPInternal::evaluate(int nfdir, int nadir) {
  int info;
  
  // Copy b vector
  for (int i=0;i<m_;++i) {
    info = DSDPSetDualObjective(dsdp_, i+1, -input(SDP_B).at(i));
  }
  
  // Get Ai from supplied A
  mapping_.setInput(input(SDP_C),0);
  mapping_.setInput(input(SDP_A),1);
  // Negate because the standard form in PSDP is different
  std::transform(mapping_.input(0).begin(), mapping_.input(0).end(), mapping_.input(0).begin(), std::negate<double>());
  std::transform(mapping_.input(1).begin(), mapping_.input(1).end(), mapping_.input(1).begin(), std::negate<double>());
  mapping_.evaluate();

  for (int i=0;i<m_+1;++i) {
    for (int j=0;j<nb_;++j) {
      mapping_.output(i*nb_+j).get(values_[i][j],SPARSESYM);
      info = SDPConeSetASparseVecMat(sdpcone_, j, i, block_sizes_[j], 1, 0, &pattern_[i][j][0], &values_[i][j][0], pattern_[i][j].size() );
    }
  }
  
  info = DSDPSetup(dsdp_);
  info = DSDPSolve(dsdp_);

  casadi_assert_message(info==0,"DSDPSolver failed");
  
  
  DSDPTerminationReason reason;
  DSDPStopReason(dsdp_, &reason);
  std::cout << "Termination reason: " << (*terminationReason_.find(reason)).second << std::endl;
  
  DSDPSolutionType pdfeasible;
  DSDPGetSolutionType(dsdp_,&pdfeasible);
  std::cout << "Solution type: " << (*solutionType_.find(pdfeasible)).second << std::endl;
  
  info = DSDPGetY(dsdp_,&output(SDP_PRIMAL).at(0),m_);
  
  double temp;
  DSDPGetDDObjective(dsdp_, &temp);
  output(SDP_PRIMAL_COST).set(-temp);
  DSDPGetPPObjective(dsdp_, &temp);
  output(SDP_DUAL_COST).set(-temp);
  
  if (calc_dual_) {
    for (int j=0;j<nb_;++j) {
      info = SDPConeComputeX(sdpcone_, j, block_sizes_[j], &store_X_[j][0], store_X_[j].size());
      Pmapper_.input(j).set(store_X_[j],SPARSESYM);
    }
    Pmapper_.evaluate();
    std::copy(Pmapper_.output().data().begin(),Pmapper_.output().data().end(),output(SDP_DUAL).data().begin());
  }
  
  if (calc_p_) {
    for (int j=0;j<nb_;++j) {
      info = SDPConeComputeS(sdpcone_, j, 1.0,  &output(SDP_PRIMAL).at(0), m_, 0, block_sizes_[j] , &store_P_[j][0], store_P_[j].size());
      Pmapper_.input(j).set(store_P_[j],SPARSESYM);
    }
    Pmapper_.evaluate();
    std::copy(Pmapper_.output().data().begin(),Pmapper_.output().data().end(),output(SDP_PRIMAL_P).data().begin());
  }
  

  
}

} // namespace CasADi
