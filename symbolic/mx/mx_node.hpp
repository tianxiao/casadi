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

#ifndef MX_NODE_HPP
#define MX_NODE_HPP

#include "mx.hpp"
#include "../sx/sx.hpp"
#include "../casadi_math.hpp"
#include "../fx/code_generator.hpp"
#include <vector>
#include <stack>

namespace CasADi{
  //@{
  /** \brief Convenience function, convert vectors to vectors of pointers */
  template<class T>
  std::vector<T*> ptrVec(std::vector<T>& v){
    std::vector<T*> ret(v.size());
    for(int i=0; i<v.size(); ++i) 
      ret[i] = &v[i];
    return ret;
  }

  template<class T>
  const std::vector<T*> ptrVec(const std::vector<T>& v){
    std::vector<T*> ret(v.size());
    for(int i=0; i<v.size(); ++i) 
      ret[i] = const_cast<T*>(&v[i]);
    return ret;
  }
  
  template<class T>
  std::vector<std::vector<T*> > ptrVec(std::vector<std::vector<T> >& v){
    std::vector<std::vector<T*> > ret(v.size());
    for(int i=0; i<v.size(); ++i) 
      ret[i] = ptrVec(v[i]);
    return ret;
  }
  
  template<class T>
  const std::vector<std::vector<T*> > ptrVec(const std::vector<std::vector<T> >& v){
    std::vector<std::vector<T*> > ret(v.size());
    for(int i=0; i<v.size(); ++i) 
      ret[i] = ptrVec(v[i]);
    return ret;
  }
  //@}

  
  /** \brief Node class for MX objects
    \author Joel Andersson 
    \date 2010
    Internal class.
*/
class MXNode : public SharedObjectNode{
  friend class MX;
  friend class MXFunctionInternal;
  
  public:
    /// Constructor
    MXNode();
  
    /** \brief  Destructor */
    virtual ~MXNode()=0;

    /** \brief  Clone function */
    virtual MXNode* clone() const = 0;

    /** \brief Check the truth value of this node
     */
    virtual bool __nonzero__() const;

    /** \brief  Deep copy data members */
    virtual void deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied);
    
    /** \brief  Print a representation */
    virtual void repr(std::ostream &stream) const;
    
    /** \brief  Print a description */
    virtual void print(std::ostream &stream) const;
    
    /** \brief  Print expression (make sure number of calls is not exceeded) */
    virtual void print(std::ostream &stream, long& remaining_calls) const;

    /** \brief  Print a part of the expression */
    virtual void printPart(std::ostream &stream, int part) const = 0;

    /** \brief Generate code for the operation */
    virtual void generateOperation(std::ostream &stream, const std::vector<std::string>& arg, const std::vector<std::string>& res, CodeGenerator& gen) const;
    
    /** \brief  Evaluate the function */
    virtual void evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output, 
                           const DMatrixPtrVV& fwdSeed, DMatrixPtrVV& fwdSens, 
                           const DMatrixPtrVV& adjSeed, DMatrixPtrVV& adjSens) = 0;

    /** \brief  Evaluate the function, no derivatives*/
    void evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output);

    /** \brief  Evaluate symbolically (SX) */
    virtual void evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output, 
                            const SXMatrixPtrVV& fwdSeed, SXMatrixPtrVV& fwdSens, 
                            const SXMatrixPtrVV& adjSeed, SXMatrixPtrVV& adjSens) = 0;

    /** \brief  Evaluate symbolically (SX), no derivatives */
    void evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output);

    /** \brief  Evaluate symbolically (MX) */
    virtual void evaluateMX(const MXPtrV& input, MXPtrV& output, 
                            const MXPtrVV& fwdSeed, MXPtrVV& fwdSens, 
                            const MXPtrVV& adjSeed, MXPtrVV& adjSens, bool output_given)=0;

    /** \brief  Evaluate symbolically (MX), no derivatives */
    void evaluateMX(const MXPtrV& input, MXPtrV& output);
    
    /** \brief  Propagate sparsity */
    virtual void propagateSparsity(DMatrixPtrV& input, DMatrixPtrV& output, bool fwd) = 0;

    /** \brief  Get the name */
    virtual const std::string& getName() const;
    
    /** \brief  Check if evaluation output */
    virtual bool isOutputNode() const{return false;}

    /** \brief  Check if a multiple output node */
    virtual bool isMultipleOutput() const{return false;}

    /** \brief  Get function reference */
    virtual FX& getFunction();

    /** \brief  Get function reference */
    virtual const FX& getFunction() const{ return const_cast<MXNode*>(this)->getFunction();}

    /** \brief  Get function input */
    virtual int getFunctionInput() const;

    /** \brief  Get function output */
    virtual int getFunctionOutput() const;

    /** \brief Get the operation */
    virtual int getOp() const = 0;

    /** \brief  dependencies - functions that have to be evaluated before this one */
    const MX& dep(int ind=0) const;
    MX& dep(int ind=0);
    
    /** \brief  Number of dependencies */
    int ndep() const;
    
    /** \brief  Does the node depend on other nodes*/
    virtual bool hasDep() const{return ndep()>0; }
    
    /** \brief  Number of outputs */
    virtual int getNumOutputs() const{ return 1;}
    
    /** \brief  Get an output */
    virtual MX getOutput(int oind) const;

    /// Get the sparsity
    const CRSSparsity& sparsity() const;

    /// Get the sparsity of output oind
    virtual const CRSSparsity& sparsity(int oind) const;
    
    /** \brief Is the node nonlinear */
    virtual bool isNonLinear(){return false;}
    
    /// Set the sparsity
    void setSparsity(const CRSSparsity& sparsity);
    
    /// Set unary dependency
    void setDependencies(const MX& dep);
    
    /// Set binary dependencies
    void setDependencies(const MX& dep1, const MX& dep2);
    
    /// Set ternary dependencies
    void setDependencies(const MX& dep1, const MX& dep2, const MX& dep3);
    
    /// Set multiple dependencies
    void setDependencies(const std::vector<MX>& dep);
        
    /// Add a dependency
    int addDependency(const MX& dep);
    
    /// Assign nonzeros (mapping matrix)
    virtual void assign(const MX& d, const std::vector<int>& inz, const std::vector<int>& onz, bool add=false);
    
    /// Assign nonzeros (mapping matrix), output indices sequential
    virtual void assign(const MX& d, const std::vector<int>& inz, bool add=false);

    /// Number of elements
    int numel() const;
    
    /// Get size
    int size() const;
    
    /// Get size
    int size1() const;
    
    /// Get size
    int size2() const;
    
    /// Convert vector of pointers to vector of objects
    template<typename T>
    static std::vector<T> getVector(const std::vector<T*> v);

    /// Convert vector of vectors of pointers to vector of vectors of objects
    template<typename T>
    static std::vector<std::vector<T> > getVector(const std::vector<std::vector<T*> > v);

    /** Temporary variables to be used in user algorithms like sorting, 
    the user is resposible of making sure that use is thread-safe
    The variable is initialized to zero
    */
    int temp;
    
    /** \brief  dependencies - functions that have to be evaluated before this one */
    std::vector<MX> dep_;
    
    /** \brief  The sparsity pattern */
    CRSSparsity sparsity_;
};

// Implementations

template<typename T>
std::vector<T> MXNode::getVector(const std::vector<T*> v){
	std::vector<T> ret(v.size());
	for(int i=0; i<v.size(); i++){
		if(v[i]!=0){
			ret[i] = *v[i];
		}
	}
	return ret;
}

template<typename T>
std::vector<std::vector<T> > MXNode::getVector(const std::vector<std::vector<T*> > v){
	std::vector<std::vector<T> > ret(v.size());
	for(int i=0; i<v.size(); i++){
		ret[i] = getVector(v[i]);
	}
	return ret;
}


} // namespace CasADi


#endif // MX_NODE_HPP
