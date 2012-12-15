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

#ifndef SQP_INTERNAL_HPP
#define SQP_INTERNAL_HPP

#include "sqp_method.hpp"
#include "symbolic/fx/nlp_solver_internal.hpp"
#include "symbolic/fx/qp_solver.hpp"

namespace CasADi{
    
class SQPInternal : public NLPSolverInternal{

public:
  explicit SQPInternal(const FX& F, const FX& G, const FX& H, const FX& J);
  virtual ~SQPInternal();
  virtual SQPInternal* clone() const{ return new SQPInternal(*this);}
  
  virtual void init();
  virtual void evaluate(int nfdir, int nadir);
  
  /// QP solver for the subproblems
  QPSolver qp_solver_;

  /// maximum number of sqp iterations
  int maxiter_; 

  /// Memory size of L-BFGS method
  int lbfgs_memory_;
  /// Tolerance of primal infeasibility
  double tol_pr_;
  /// Tolerance of dual infeasibility
  double tol_du_;

  /// Linesearch parameters
  //@{
  double sigma_;
  double c1_;
  double beta_;
  int maxiter_ls_;
  int merit_memsize_;
  //@}

  /// Access QPSolver
  const QPSolver getQPSolver() const { return qp_solver_;}
  
  /// Lagrange multipliers of the NLP
  std::vector<double> mu_, mu_x_;
  
  /// Current cost function value
  double fk_;
  
  /// Current and previous linearization point and candidate
  std::vector<double> x_, x_old_, x_cand_;
  
  /// Lagrange gradient in the next iterate
  std::vector<double> gLag_, gLag_old_;
  
  /// Constraint function value
  std::vector<double> gk_, gk_cand_;
  
  /// BFGS update function
  enum{ BFGS_BK, BFGS_X, BFGS_X_OLD, BFGS_GLAG, BFGS_GLAG_OLD, BFGS_NUM_IN}; 
  FX bfgs_;
  
  /// Current Hessian approximation
  DMatrix Bk_;
  
  /// Calculates inner_prod(x,mul(A,x))
  static double quad_form(const std::vector<double>& x, const DMatrix& A);
  
  /// Print iteration header
  void printIteration(std::ostream &stream);
  
  /// Print iteration
  void printIteration(std::ostream &stream, int iter, double obj, double pr_inf, double du_inf, 
                      double corr_norm, double ls_param, bool ls_success, int ls_trials);
};

} // namespace CasADi

#endif //SQP_INTERNAL_HPP
