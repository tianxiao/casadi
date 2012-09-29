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

#ifndef MX_FUNCTION_INTERNAL_HPP
#define MX_FUNCTION_INTERNAL_HPP

#include <set>
#include <map>
#include <vector>
#include <iostream>

#include "mx_function.hpp"
#include "x_function_internal.hpp"
#include "../mx/mx_node.hpp"

namespace CasADi{

/** \brief  Internal node class for MXFunction
  \author Joel Andersson 
  \date 2010
*/
class MXFunctionInternal : public XFunctionInternal<MXFunctionInternal,MX,MXNode>{
  friend class MXFunction;
  
  public:

    /** \brief  Multiple input, multiple output constructor, only to be accessed from MXFunction, therefore protected */
    MXFunctionInternal(const std::vector<MX>& input, const std::vector<MX>& output);

    /** \brief  Make a deep copy */
    virtual MXFunctionInternal* clone() const;

    /** \brief  Deep copy data members */
    virtual void deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied);
    
    /** \brief  Destructor */
    virtual ~MXFunctionInternal();

    /** \brief  Evaluate the algorithm */
    virtual void evaluate(int nfdir, int nadir);

    /** \brief  Print description */
    virtual void print(std::ostream &stream) const;

    /** \brief  Initialize */
    virtual void init();

    /** \brief  Update the number of sensitivity directions during or after initialization */
    virtual void updateNumSens(bool recursive);
    
    /** \brief Set the lifting function */
    void setLiftingFunction(LiftingFunction liftfun, void* user_data);

    /** \brief Calculate the expression for the jacobian of a number of function outputs with respect to a number of function inputs, optionally include the function outputs */
    MX jac(int iind=0, int oind=0, bool compact=false, bool symmetric=false);

    /** \brief Generate a function that calculates nfwd forward derivatives and nadj adjoint derivatives */
    virtual FX getDerivative(int nfwd, int nadj);

    /** \brief Jacobian via source code transformation */
    virtual FX jacobian(const std::vector<std::pair<int,int> >& jblocks);
    
    /** \brief Calculate the jacobian of output oind with respect to input iind */
//     virtual FX getJacobian(int iind, int oind);

    /** \brief  An elemenent of the algorithm, namely an MX node */
    typedef MXAlgEl AlgEl;

    /** \brief  All the runtime elements in the order of evaluation */
    std::vector<AlgEl> algorithm_;

    /** \brief  Working vector for numeric calculation */
    std::vector<FunctionIO> work_;
    
    /** \brief  Dependent expressions */
    std::vector<int> input_ind_;

    /** \brief  Matrix expressions that are to be evaluated */
    std::vector<int> output_ind_;

    /// Free variables
    std::vector<MX> free_vars_;
    std::vector<int> free_vars_ind_;
    
    /// Collect the free variables
    void collectFree();
    
    // Lifting function
    LiftingFunction liftfun_;
    void* liftfun_ud_;
    
    /** \brief Hessian of output oind with respect to input iind.  */
    FX hessian(int iind, int oind);
    
    /** \brief Evaluate symbolically, SX type*/
    virtual void evalSX(const std::vector<SXMatrix>& input, std::vector<SXMatrix>& output, 
                        const std::vector<std::vector<SXMatrix> >& fwdSeed, std::vector<std::vector<SXMatrix> >& fwdSens, 
                        const std::vector<std::vector<SXMatrix> >& adjSeed, std::vector<std::vector<SXMatrix> >& adjSens,
                        bool output_given, int offset_begin=0, int offset_end=0);
                        
    /** \brief Evaluate symbolically, MX type */
    virtual void evalMX(const std::vector<MX>& input, std::vector<MX>& output, 
                        const std::vector<std::vector<MX> >& fwdSeed, std::vector<std::vector<MX> >& fwdSens, 
                        const std::vector<std::vector<MX> >& adjSeed, std::vector<std::vector<MX> >& adjSens,
                        bool output_given);

    /** \brief Expand the matrix valued graph into a scalar valued graph */
    SXFunction expand(const std::vector<SXMatrix>& inputv );
    
    // Update pointers to a particular element
    void updatePointers(const AlgEl& el, int nfwd, int nadj);
    
    // Vectors to hold pointers during evaluation
    DMatrixPtrV mx_input_;
    DMatrixPtrV mx_output_;
    DMatrixPtrVV mx_fwdSeed_;
    DMatrixPtrVV mx_fwdSens_;
    DMatrixPtrVV mx_adjSeed_;
    DMatrixPtrVV mx_adjSens_;

    /// Get a vector of symbolic variables with the same dimensions as the inputs
    virtual std::vector<MX> symbolicInput() const{ return inputv_;}

    /// Propagate a sparsity pattern through the algorithm
    virtual void spEvaluate(bool fwd);

    /// Is the class able to propate seeds through the algorithm?
    virtual bool spCanEvaluate(bool fwd){ return true;}

    /// Reset the sparsity propagation
    virtual void spInit(bool fwd);
};

} // namespace CasADi


#endif // MX_FUNCTION_INTERNAL_HPP
