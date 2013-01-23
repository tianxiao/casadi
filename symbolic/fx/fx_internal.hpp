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

#ifndef FX_INTERNAL_HPP
#define FX_INTERNAL_HPP

#include "fx.hpp"
#include "../weak_ref.hpp"
#include <set>

// This macro is for documentation purposes
#define INPUTSCHEME(name)

// This macro is for documentation purposes
#define OUTPUTSCHEME(name)

namespace CasADi{
  
/** \brief Internal class for FX
  \author Joel Andersson 
  \date 2010
 A regular user should never work with any Node class. Use FX directly.
*/
class FXInternal : public OptionsFunctionalityNode{
  friend class FX;

  protected:
    /** \brief  Default constructor (accessable from the FX class and derived classes) */
    FXInternal();
  
  public:
    /** \brief  Destructor */
    virtual ~FXInternal() = 0;

    /** \brief  Evaluate */
    virtual void evaluate(int nfdir, int nadir) = 0;
  
    /** \brief  Evaluate with directional derivative compression */
    void evaluateCompressed(int nfdir, int nadir);

    /** \brief Initialize
      Initialize and make the object ready for setting arguments and evaluation. This method is typically called after setting options but before evaluating. 
      If passed to another class (in the constructor), this class should invoke this function when initialized. */
    virtual void init();

    /** \brief  Update the number of sensitivity directions during or after initialization, 
        if recursive==true, updateNumSens is also invoked for the baseclass. */
    virtual void updateNumSens(bool recursive);
    
    /** \brief Request a number of forward/adjoint derivative directions */
    void requestNumSens(int nfwd, int nadj);
  
    /** \brief  Propagate the sparsity pattern through a set of directional derivatives forward or backward */
    virtual void spEvaluate(bool fwd);

    /** \brief  Is the class able to propate seeds through the algorithm? */
    virtual bool spCanEvaluate(bool fwd){ return false;}

    /** \brief  Reset the sparsity propagation */
    virtual void spInit(bool fwd){}
    
    /** \brief  Evaluate symbolically, SX type */
    virtual void evalSX(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
                        const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
                        const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
                        bool output_given);

    /** \brief  Evaluate symbolically, MX type */
    virtual void evalMX(const std::vector<MX>& arg, std::vector<MX>& res, 
                        const std::vector<std::vector<MX> >& fseed, std::vector<std::vector<MX> >& fsens, 
                        const std::vector<std::vector<MX> >& aseed, std::vector<std::vector<MX> >& asens,
                        bool output_given);
    
    /** \brief Call a function, MX type (overloaded) */
    void call(const MXVector& arg, MXVector& res, 
              const MXVectorVector& fseed, MXVectorVector& fsens, 
              const MXVectorVector& aseed, MXVectorVector& asens,
              bool output_given, bool always_inline, bool never_inline);
    
    /** \brief Call a function, SX type (overloaded) */
    void call(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
              const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
              const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
              bool output_given, bool always_inline, bool never_inline);
    
    //@{
    /** \brief Return Hessian function */
    FX hessian(int iind, int oind);
    virtual FX getHessian(int iind, int oind);
    //@}

    //@{
    /** \brief Return gradient function */
    FX gradient(int iind, int oind);
    virtual FX getGradient(int iind, int oind);
    //@}

    //@{
    /** \brief Return Jacobian function */
    FX jacobian(int iind, int oind, bool compact, bool symmetric);
    virtual FX getJacobian(int iind, int oind, bool compact, bool symmetric);
    virtual FX getNumericJacobian(int iind, int oind, bool compact, bool symmetric);
    //@}
    
    //@{
    /** \brief Return Jacobian of all input nonzeros with respect to all output nonzeros */
    FX fullJacobian();
    virtual FX getFullJacobian();
    //@}

    //@{
    /** \brief Return function that calculates forward derivatives 
    *    This method returns a cached instance if available, and calls FX getDerivative(int nfwd, int nadj) if no cached version is available.
    */
    FX derivative(int nfwd, int nadj);

    /** \brief Constructs and returns a function that calculates forward derivatives */
    virtual FX getDerivative(int nfwd, int nadj);

    /** \brief Constructs and returns a function that calculates forward derivatives by creating the Jacobian then multiplying */
    virtual FX getDerivativeViaJac(int nfwd, int nadj);

    //@}

    /** \brief  Access an input */
    FunctionIO& iStruct(int i){
      try{
        return input_.at(i);
      } catch(std::out_of_range&){
          std::stringstream ss;
          ss <<  "In function " << getOption("name") << ": input " << i << " not in interval [0," << getNumInputs() << ")";
          if (!isInit()) ss << std::endl << "Did you forget to initialize?";
          throw CasadiException(ss.str());
      }
    }

    /** \brief  Const access an input */
    inline const FunctionIO& iStruct(int i) const{
      return const_cast<FXInternal*>(this)->iStruct(i);
    }
    
    /** \brief  Access an output*/
    FunctionIO& oStruct(int i){
      try{
        return output_.at(i);
      } catch(std::out_of_range&){
          std::stringstream ss;
          ss <<  "In function " << getOption("name") << ": output " << i << " not in interval [0," << getNumOutputs() << ")";
          if (!isInit()) ss << std::endl << "Did you forget to initialize?";
          throw CasadiException(ss.str());
      }
    }

    /** \brief  Const access an output*/
    inline const FunctionIO& oStruct(int i) const{
      return const_cast<FXInternal*>(this)->oStruct(i);
    }
      
    /** \brief  Print */
    virtual void print(std::ostream &stream) const;
    
    /** \brief  Print */
    virtual void repr(std::ostream &stream) const;
    
    /** \brief Find the index for a string describing a particular entry of an input scheme
    * example:  schemeEntry("x_opt")  -> returns  NLP_X_OPT if FXInternal adheres to SCHEME_NLPINput 
    */
    int inputSchemeEntry(const std::string &name) const;

    /** \brief Find the index for a string describing a particular entry of an output scheme
    * example:  schemeEntry("x_opt")  -> returns  NLP_X_OPT if FXInternal adheres to SCHEME_NLPINput 
    */
    int outputSchemeEntry(const std::string &name) const;

    /** \brief Find the index for a string describing a particular entry of a scheme
    * example:  schemeEntry("x_opt")  -> returns  NLP_X_OPT if FXInternal adheres to SCHEME_NLPINput 
    */
    int schemeEntry(InputOutputScheme scheme,const std::string &name) const;
    
    /** \brief  Inputs of the function */
    std::vector<FunctionIO> input_;

    /** \brief  Output of the function */
    std::vector<FunctionIO> output_;

    /** \brief Get the unidirectional or bidirectional partition */
    void getPartition(int iind, int oind, CRSSparsity& D1, CRSSparsity& D2, bool compact, bool symmetric);

    /// Verbose mode?
    bool verbose() const;
    
    /// Is function fcn being monitored
    bool monitored(const std::string& mod) const;
    
    //@{
      /// Access input argument
    inline Matrix<double>& input(int iind=0){ return iStruct(iind).data;}
    inline Matrix<double>& input(const std::string &iname){ return input(inputSchemeEntry(iname));}
    inline Matrix<double>& inputNoCheck(int iind=0){ return input_[iind].data;}
    inline const Matrix<double>& input(int iind=0) const{ return iStruct(iind).data;}
    inline const Matrix<double>& input(const std::string &iname) const{  return input(inputSchemeEntry(iname)); }
    inline const Matrix<double>& inputNoCheck(int iind=0) const{ return input_[iind].data;}
    //@{
    
    //@{
    /// Access output argument
    inline Matrix<double>& output(int oind=0){ return oStruct(oind).data;}
    inline Matrix<double>& outputNoCheck(int oind=0){ return output_[oind].data;}
    inline const Matrix<double>& output(int oind=0) const{ return oStruct(oind).data;}
    inline const Matrix<double>& outputNoCheck(int oind=0) const{ return output_[oind].data;}
    //@{

    //@{
    /// Access forward seed
    Matrix<double>& fwdSeed(int iind=0, int dir=0){
      try{
        return iStruct(iind).dataF.at(dir);
      } catch(std::out_of_range&){
        std::stringstream ss;
        if(iStruct(iind).dataF.empty()){
          ss << "No forward directions ";
        } else {
          ss << "Forward direction " << dir << " is out of range [0," << iStruct(iind).dataF.size() << ") ";
        }
        ss << "for function " << getOption("name");
        throw CasadiException(ss.str());
      }
    }
    Matrix<double>& fwdSeedNoCheck(int iind=0, int dir=0){ return input_[iind].dataF[dir]; }
    const Matrix<double>& fwdSeed(int iind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->fwdSeed(iind,dir); }
    const Matrix<double>& fwdSeedNoCheck(int iind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->fwdSeedNoCheck(iind,dir); }
    //@}

    //@{
    /// Access forward sensitivity
    Matrix<double>& fwdSens(int oind=0, int dir=0){
      try{
        return oStruct(oind).dataF.at(dir);
      } catch(std::out_of_range&){
        std::stringstream ss;
        if(oStruct(oind).dataF.empty()){
          ss << "No forward directions ";
        } else {
          ss << "Forward direction " << dir << " is out of range [0," << oStruct(oind).dataF.size() << ") ";
        }
        ss << "for function " << getOption("name");
        throw CasadiException(ss.str());
      }
    }
    Matrix<double>& fwdSensNoCheck(int oind=0, int dir=0){ return output_[oind].dataF[dir]; }
    const Matrix<double>& fwdSens(int oind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->fwdSens(oind,dir);}
    const Matrix<double>& fwdSensNoCheck(int oind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->fwdSensNoCheck(oind,dir);}
    //@}

    //@{
    /// Access adjoint seed
    Matrix<double>& adjSeed(int oind=0, int dir=0){
      try{
        return oStruct(oind).dataA.at(dir);
      } catch(std::out_of_range&){
        std::stringstream ss;
        if(oStruct(oind).dataA.empty()){
          ss << "No adjoint directions ";
        } else {
          ss << "Adjoint direction " << dir << " is out of range [0," << oStruct(oind).dataA.size() << ") ";
        }
        ss << "for function " << getOption("name");
        throw CasadiException(ss.str());
      }
    }
    Matrix<double>& adjSeedNoCheck(int oind=0, int dir=0){ return output_[oind].dataA[dir];}
    const Matrix<double>& adjSeed(int oind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->adjSeed(oind,dir);}
    const Matrix<double>& adjSeedNoCheck(int oind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->adjSeedNoCheck(oind,dir);}
    //@}

    //@{
    /// Access forward sensitivity
    Matrix<double>& adjSens(int iind=0, int dir=0){
      try{
        return iStruct(iind).dataA.at(dir);
      } catch(std::out_of_range&){
        std::stringstream ss;
        if(iStruct(iind).dataA.empty()){
          ss << "No adjoint directions ";
        } else {
          ss << "Adjoint direction " << dir << " is out of range [0," << iStruct(iind).dataA.size() << ") ";
        }
        ss << "for function " << getOption("name");
        throw CasadiException(ss.str());
      }
    }
    Matrix<double>& adjSensNoCheck(int iind=0, int dir=0){ return input_[iind].dataA[dir];}
    const Matrix<double>& adjSens(int iind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->adjSens(iind,dir);}
    const Matrix<double>& adjSensNoCheck(int iind=0, int dir=0) const{ return const_cast<FXInternal*>(this)->adjSensNoCheck(iind,dir);}
    //@}

    /// Set the number of function inputs
    void setNumInputs(int num_in);

    /// Set the number of function outputs
    void setNumOutputs(int num_out);

    /// Get the number of function inputs
    inline int getNumInputs() const{ return input_.size();}

    /// Get the number of function outputs
    inline int getNumOutputs() const{ return output_.size();}
    
    /// Get total number of scalar inputs (i.e. the number of nonzeros in all of the matrix-valued inputs)
    int getNumScalarInputs() const;

    /// Get total number of scalar outputs (i.e. the number of nonzeros in all of the matrix-valued outputs)
    int getNumScalarOutputs() const;
    
    /// Get all statistics obtained at the end of the last evaluate call
    const Dictionary & getStats() const;

    /// Get single statistic obtained at the end of the last evaluate call
    GenericType getStat(const std::string & name) const;
    
    /// Generate the sparsity of a Jacobian block
    virtual CRSSparsity getJacSparsity(int iind, int oind, bool symmetric);
    
    /// A flavour of getJacSparsity without any magic
    CRSSparsity getJacSparsityPlain(int iind, int oind);
    
    /// A flavour of getJacSparsity that does hierachical block structure recognition
    CRSSparsity getJacSparsityHierarchical(int iind, int oind);
    
    /// A flavour of getJacSparsity that does hierachical block structure recognition for symmetric jacobians
    CRSSparsity getJacSparsityHierarchicalSymm(int iind, int oind);
    
    /// Generate the sparsity of a Jacobian block
    void setJacSparsity(const CRSSparsity& sp, int iind, int oind, bool compact);
    
    /// Get, if necessary generate, the sparsity of a Jacobian block
    CRSSparsity& jacSparsity(int iind, int oind, bool compact, bool symmetric);
    
    /// Get a vector of symbolic variables with the same dimensions as the inputs
    virtual std::vector<MX> symbolicInput() const;
  
    /// Get a vector of symbolic variables with the same dimensions as the inputs
    virtual std::vector<SXMatrix> symbolicInputSX() const;
  
    /** \brief  Number of forward and adjoint derivatives */
    int nfdir_, nadir_;

    /** \brief  Verbose -- for debugging purposes */
    bool verbose_;
    
    /** \brief  Log the status of the solver */
    void log(const std::string& msg) const;

    /** \brief  Log the status of the solver, function given */
    void log(const std::string& fcn, const std::string& msg) const;

    /// Set of module names which are extra monitored
    std::set<std::string> monitors_;
    
    /** \brief  Dictionary of statistics (resulting from evaluate) */
    Dictionary stats_;
    
    /** \brief  Flag to indicate wether statistics must be gathered */
    bool gather_stats_;

    /// Cache for functions to evaluate directional derivatives
    std::vector<std::vector<FX> > derivative_fcn_; // NOTE: This can result in circular dependencies!

    /// Cache for full Jacobian
    WeakRef full_jacobian_;

    /// Cache for sparsities of the Jacobian blocks
    std::vector<std::vector<CRSSparsity> > jac_sparsity_, jac_sparsity_compact_;

    /// Which derivative directions are currently being compressed
    std::vector<bool> compressed_fwd_, compressed_adj_;

    /// User-provided Jacobian generator function
    JacobianGenerator jacgen_;

    /// User-provided sparsity generator function
    SparsityGenerator spgen_;

    /// User-set field
    void* user_data_;
    
    bool monitor_inputs_, monitor_outputs_;
    
    /// The name of the input scheme of this function
    InputOutputScheme inputScheme;
    
    /// The name of the output scheme of this function
    InputOutputScheme outputScheme;
    
    /// Errors are thrown when NaN is produced
    bool regularity_check_;
    
};


} // namespace CasADi


#endif // FX_INTERNAL_HPP
