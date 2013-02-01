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

#ifndef SX_FUNCTION_HPP
#define SX_FUNCTION_HPP

#include "fx.hpp"

namespace CasADi{

#ifndef SWIG

/** \brief  An atomic operation for the SX virtual machine */
struct SXAlgEl{
  /// Operator index
  int op; 
  
  /// Output argument (typically the index of the result)
  int res;
  
  /// Input argument
  union{
    
    /// Floating point constant
    double d;
  
    /// Integer constant (typically the indices of the arguments)
    int i[2]; 

  } arg;
};

#endif // SWIG

/// Forward declaration of internal class
class SXFunctionInternal;

/// Forward declaration of MXFunction
class MXFunction;

/**   \brief Dynamically created function that can be expanded into a series of scalar operations.
\author Joel Andersson 
\date 2010
*/

class SXFunction : public FX{

public:
  /// Default constructor
  SXFunction();
  
  /// Expand an MXFunction
  explicit SXFunction(const MXFunction &f);

  /// Expand an FX
  explicit SXFunction(const FX &f);
  
  /// Multiple (matrix valued) input, multiple (matrix valued) output 
  SXFunction(const std::vector< SXMatrix>& arg, const std::vector<SXMatrix>& res);

#ifndef SWIG

  /// Multiple (vector valued) input, multiple (vector valued) output 
  SXFunction(const std::vector< std::vector<SX> >& arg, const std::vector< std::vector<SX> >& res);

  /// Single (scalar/matrix/vector valued) input, single (scalar/matrix/vector valued) output  
  SXFunction(const SXMatrix& arg, const SXMatrix& res);

  /// Multiple (vector valued) input, single (scalar/vector/matrix valued) output 
  SXFunction(const std::vector< std::vector<SX> >& arg, const SXMatrix& res);

  /// Multiple (matrix valued) input, single (scalar/vector/matrix valued) output 
  SXFunction(const std::vector< SXMatrix>& arg, const SXMatrix& res);

  /// Single (scalar/vector/matrix valued) input, multiple (vector valued) output 
  SXFunction(const SXMatrix& arg, const std::vector< std::vector<SX> >& res);

  /// Single (scalar/vector/matrix valued) input, multiple (matrix valued) output 
  SXFunction(const SXMatrix& arg, const std::vector< SXMatrix>& res);
#endif // SWIG

  /// Access functions of the node 
  SXFunctionInternal* operator->();

  /// Const access functions of the node 
  const SXFunctionInternal* operator->() const;

  /** \brief Jacobian via source code transformation
  *
  * \see CasADi::Jacobian for an AD approach
  */
  SXMatrix jac(int iind=0, int oind=0, bool compact=false, bool symmetric=false);

  /// Gradient via source code transformation
  SXMatrix grad(int iind=0, int oind=0);
  
  /// Hessian (forward over adjoint) via source code transformation
  SXMatrix hess(int iind=0, int oind=0);
  
  /// Check if the node is pointing to the right type of object
  virtual bool checkNode() const;
    
  /** \brief Get function input */
  const SXMatrix& inputExpr(int ind) const;
  
  /** \brief Get function output */
  const SXMatrix& outputExpr(int ind) const;
  
  /** \brief Get all function inputs */
  const std::vector<SXMatrix>& inputExpr() const;
  
  /** \brief Get all function outputs */
  const std::vector<SXMatrix> & outputExpr() const;
    
#ifndef SWIG
  /** \brief Access the algorithm directly */
  const std::vector<SXAlgEl>& algorithm() const;
#endif // SWIG
  
  /** \brief Get the number of atomic operations */
  int getAlgorithmSize() const{ return algorithm().size();}

  /** \brief Get the length of the work vector */
  int getWorkSize() const;

  /** \brief Get an atomic operation operator index */
  int getAtomicOperation(int k) const{ return algorithm().at(k).op;}

  /** \brief Get the (integer) input arguments of an atomic operation */
  std::pair<int,int> getAtomicInput(int k) const{ const int* i = algorithm().at(k).arg.i; return std::pair<int,int>(i[0],i[1]);}

  /** \brief Get the floating point output argument of an atomic operation */
  double getAtomicInputReal(int k) const{ return algorithm().at(k).arg.d;}

  /** \brief Get the (integer) output argument of an atomic operation */
  int getAtomicOutput(int k) const{ return algorithm().at(k).res;}

  /** \brief Number of nodes in the algorithm */
  int countNodes() const;
  
  /** \brief Clear the function from its symbolic representation, to free up memory, no symbolic evaluations are possible after this */
  void clearSymbolic();
 
  /** \brief Get all the free variables of the function */
  std::vector<SX> getFree() const;

  /** \brief Get the corresponding matrix type */
  typedef SXMatrix MatType;  
  
#ifndef SWIG 
  /// Construct a function that has only the k'th output
  SXFunction operator[](int k) const;
#endif //SWIG 

SXFunction indexed_one_based(int k) const{ return operator[](k-1);}
SXFunction indexed_zero_based(int k) const{ return operator[](k);}

};

} // namespace CasADi

#endif // SX_FUNCTION_HPP
