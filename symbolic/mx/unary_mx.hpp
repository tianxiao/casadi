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

#ifndef UNARY_MX_HPP
#define UNARY_MX_HPP

#include "mx_node.hpp"

namespace CasADi{
/** \brief Represents a general unary operation on an MX
  \author Joel Andersson 
  \date 2010
*/
class UnaryMX : public MXNode{
private:
    
    /** \brief  Constructor is private, use "create" below */
    UnaryMX(Operation op, MX x);

public:
  
  /** \brief  Create a unary expression */
  static MX create(int op, const MX& x);

  /** \brief  Destructor */
  virtual ~UnaryMX(){}

  /** \brief  Clone function */
  virtual UnaryMX * clone() const;

  /** \brief  Print a part of the expression */
  virtual void printPart(std::ostream &stream, int part) const;

  /** \brief  Evaluate the function numerically */
  virtual void evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output, const DMatrixPtrVV& fwdSeed, DMatrixPtrVV& fwdSens, const DMatrixPtrVV& adjSeed, DMatrixPtrVV& adjSens);

  /** \brief  Evaluate the function symbolically (SX) */
  virtual void evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output, const SXMatrixPtrVV& fwdSeed, SXMatrixPtrVV& fwdSens, const SXMatrixPtrVV& adjSeed, SXMatrixPtrVV& adjSens);

  /** \brief  Evaluate the function symbolically (MX) */
  virtual void evaluateMX(const MXPtrV& input, MXPtrV& output, const MXPtrVV& fwdSeed, MXPtrVV& fwdSens, const MXPtrVV& adjSeed, MXPtrVV& adjSens, bool output_given);

  /** \brief  Propagate sparsity */
  virtual void propagateSparsity(DMatrixPtrV& input, DMatrixPtrV& output, bool fwd);

  /** \brief Get the operation */
  virtual int getOp() const{ return op_;}
    
  /** \brief Generate code for the operation */
  virtual void generateOperation(std::ostream &stream, const std::vector<std::string>& arg, const std::vector<std::string>& res, CodeGenerator& gen) const;

  //! \brief operation
  Operation op_;
};

} // namespace CasADi


#endif // UNARY_MX_HPP
