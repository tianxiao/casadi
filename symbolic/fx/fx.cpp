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

#include "fx_internal.hpp"
#include "../mx/evaluation_mx.hpp"
#include "../sx/evaluation_sx.hpp"
#include "../fx/mx_function.hpp"
#include <typeinfo> 
#include "../stl_vector_tools.hpp"
#include "../matrix/matrix_tools.hpp"
#include "jacobian.hpp"
#include "parallelizer.hpp"

using namespace std;

namespace CasADi{
  
FX::FX(){
}

FX::~FX(){
}

FX FX::create(FXInternal* node){
  FX ret;
  ret.assignNode(node);
  return ret;
}

const FXInternal* FX::operator->() const{
  return (const FXInternal*)OptionsFunctionality::operator->();
}

FXInternal* FX::operator->(){
  return (FXInternal*)OptionsFunctionality::operator->();
}

vector<SXMatrix> FX::callSX(const std::vector<SXMatrix> &arg){
  casadi_warning("FX::callSX is currently only experimental.");
  
  // Make sure that the number of inputs matches
  casadi_assert_message(arg.size()<=getNumInputs(),"Too many input arguments.");
  
  // Assemble all the scalar inputs
  vector<SX> f_in(getNumScalarInputs());
  SX* f_in_local = getPtr(f_in);
  for(int iind=0; iind<arg.size(); ++iind){
    const CRSSparsity& sp_in = input(iind).sparsity();
    sp_in.set(f_in_local,getPtr(arg[iind].data()),arg[iind].sparsity());
    f_in_local += sp_in.size();
  }
  
  // Make sure that there are no unset input arguments
  for(int iind=arg.size(); iind<getNumInputs(); ++iind){
    casadi_assert_message(input(iind).empty(),"Nonzeros not set for input argument " << iind << ".");
  }
  
  // Create call node
  vector<SX> f_out = EvaluationSX::create(*this,f_in);
  
  // Return value
  vector<SX>::const_iterator f_out_this = f_out.begin(), f_out_next;
  vector<SXMatrix> ret(getNumOutputs());
  for(int oind=0; oind<getNumOutputs(); ++oind){
    f_out_next = f_out_this+output(oind).size();
    ret[oind] = SXMatrix(output(oind).sparsity(),vector<SX>(f_out_this,f_out_next));
    f_out_this = f_out_next;
  }
  
  return ret;
}

vector<MX> FX::call(const MX &arg){
  vector<MX> xvec(1,arg);
  return call(xvec);
}

vector<MX> FX::call(const vector<MX> &arg){
  MXVectorVector dummy;
  MXVector res;
  call(arg,res,dummy,dummy,dummy,dummy);
  return res;
}

void FX::call(const MXVector& arg, MXVector& res,  const MXVectorVector& fseed, MXVectorVector& fsens, 
	      const MXVectorVector& aseed, MXVectorVector& asens, bool output_given){
  assertInit();
  
  // Argument checking
  casadi_assert_message(arg.size()<=getNumInputs(), "FX::call: number of passed-in dependencies (" << arg.size() << ") should not exceed the number of inputs of the function (" << getNumInputs() << ").");

  // Assumes initialised
  for(int i=0; i<arg.size(); ++i){
    if(arg[i].isNull() || arg[i].empty()) continue;
    casadi_assert_message(arg[i].size1()==input(i).size1() && arg[i].size2()==input(i).size2(),
			  "Evaluation::shapes of passed-in dependencies should match shapes of inputs of function." << 
			  std::endl << "Input argument " << i << " has shape (" << input(i).size1() << 
			  "," << input(i).size2() << ") while a shape (" << arg[i].size1() << "," << arg[i].size2() << 
			  ") was supplied.");
  }
  
  EvaluationMX::create(*this,arg,res,fseed,fsens,aseed,asens);
}

vector<vector<MX> > FX::call(const vector<vector<MX> > &x, const Dictionary& paropt){
  assertInit();
  
  // Make sure not empty
  casadi_assert_message(x.size()>1,"FX: call(vector<vector<MX> >): argument must be of length > 1. You supplied length " << x.size() << ".");
  
  // Return object
  vector<vector<MX> > ret(x.size());
    
  // Check if we are bypassing the parallelizer
  Dictionary::const_iterator ii=paropt.find("parallelization");
  if(ii!=paropt.end() && ii->second=="expand"){
    for(int i=0; i<x.size(); ++i){
      ret[i] = call(x[i]);
    }
    return ret;
  }
      
  // Create parallelizer object and initialize it
  Parallelizer p(vector<FX>(x.size(),*this));
  p.setOption(paropt);
  p.init();
  
  // Concatenate the arguments
  vector<MX> p_in;
  p_in.reserve(x.size() * getNumInputs());
  for(int i=0; i<x.size(); ++i){
    p_in.insert(p_in.end(),x[i].begin(),x[i].end());
    p_in.resize(p_in.size()+getNumInputs()-x[i].size());
  }
  
  // Call the parallelizer
  vector<MX> p_out = p.call(p_in);
  casadi_assert(p_out.size() == x.size() * getNumOutputs());

  // Collect the outputs
  vector<MX>::const_iterator it=p_out.begin();
  for(int i=0; i<x.size(); ++i){
    ret[i].insert(ret[i].end(),it,it+getNumOutputs());
    it += getNumOutputs();
  }
  return ret;
}

void FX::evaluate(int nfdir, int nadir){
  assertInit();
  casadi_assert(nfdir<=(*this)->nfdir_);
  casadi_assert(nadir<=(*this)->nadir_);
  (*this)->evaluate(nfdir,nadir);
}

void FX::solve(){
  evaluate(0,0);
}

int FX::getNumInputs() const{
  return (*this)->getNumInputs();
}

int FX::getNumOutputs() const{
  return (*this)->getNumOutputs();  
}

int FX::getNumScalarInputs() const{
  return (*this)->getNumScalarInputs();
}

int FX::getNumScalarOutputs() const{
  return (*this)->getNumScalarOutputs();
}


void FX::setNumInputs(int num_in){
  return (*this)->setNumInputs(num_in);
}

void FX::setNumOutputs(int num_out){
  return (*this)->setNumOutputs(num_out);  
}

FX FX::jacobian(int iind, int oind){
  assertInit();
  return (*this)->jacobian_new(iind,oind);
}

FX FX::jacobian(const std::vector<std::pair<int,int> >& jblocks){
  assertInit();
  return (*this)->jacobian_switch(jblocks);
}

FX FX::hessian(int iind, int oind){
  assertInit();
  return (*this)->hessian(iind,oind);  
}

bool FX::checkNode() const{
  return dynamic_cast<const FXInternal*>(get())!=0;
}

Matrix<double>& FX::input(int iind){
  return (*this)->input(iind);
}

Matrix<double>& FX::input(const std::string &iname){
  return (*this)->input(iname);
}
    
    
const Matrix<double>& FX::input(int iind) const{
  return (*this)->input(iind);
}

const Matrix<double>& FX::input(const std::string & iname) const{
  return (*this)->input(iname);
}

Matrix<double>& FX::output(int oind){
  return (*this)->output(oind);
}
    
const Matrix<double>& FX::output(int oind) const{
  return (*this)->output(oind);
}

Matrix<double>& FX::fwdSeed(int iind, int dir){
  return (*this)->fwdSeed(iind,dir);
}
    
const Matrix<double>& FX::fwdSeed(int iind, int dir) const{
  return (*this)->fwdSeed(iind,dir);
}

Matrix<double>& FX::fwdSens(int oind, int dir){
  return (*this)->fwdSens(oind,dir);
}
    
const Matrix<double>& FX::fwdSens(int oind, int dir) const{
  return (*this)->fwdSens(oind,dir);
}

Matrix<double>& FX::adjSeed(int oind, int dir){
  return (*this)->adjSeed(oind,dir);
}
    
const Matrix<double>& FX::adjSeed(int oind, int dir) const{
  return (*this)->adjSeed(oind,dir);
}

Matrix<double>& FX::adjSens(int iind, int dir){
  return (*this)->adjSens(iind,dir);
}
    
const Matrix<double>& FX::adjSens(int iind, int dir) const{
  return (*this)->adjSens(iind,dir);
}

void FX::addMonitor(const string& mon){
  (*this)->monitors_.insert(mon);
}

void FX::removeMonitor(const string& mon){
  (*this)->monitors_.erase(mon);
}

const Dictionary & FX::getStats() const{
  return (*this)->getStats();
}

GenericType FX::getStat(const string& name) const{
  return (*this)->getStat(name);
}

CRSSparsity& FX::jacSparsity(int iind, int oind, bool compact){
  return (*this)->jacSparsity(iind,oind,compact);
}

void FX::setJacSparsity(const CRSSparsity& sp, int iind, int oind, bool compact){
  (*this)->setJacSparsity(sp,iind,oind,compact);
}

std::vector<MX> FX::symbolicInput() const{
  return (*this)->symbolicInput();
}

std::vector<SXMatrix> FX::symbolicInputSX() const{
  return (*this)->symbolicInputSX();
}

FX FX::operator[](int k) const {

  // Argument checking
  if (k<0) k+=getNumOutputs();
  casadi_assert_message(k<getNumOutputs(),"FX[int k]:: Attempt to select the k'th output with k=" << k << ", but should be smaller or equal to number of outputs (" << getNumOutputs() << ").");
  
  // Get the inputs in MX form
  std::vector< MX > in = symbolicInput();
  
  // Clone such that we can use const.
  FX clone = *this;
  
  // Get the outputs
  std::vector< MX > result = clone.call(in);
  
  // Construct an MXFunction with only the k'th output
  MXFunction ret(in,result[k]);
  
  // Initialize it
  ret.init();
  
  // And return it, will automatically cast to FX
  return ret;
}

void FX::updateNumSens(){
  return (*this)->updateNumSens(true);
}

vector<SXMatrix> FX::evalSX(const vector<SXMatrix>& arg){
  casadi_assert_message(isInit(),"Function has not been initialized");
  
  // Copy the arguments into a new vector with the right sparsity
  casadi_assert_message(arg.size()<=getNumInputs(), "FX::evalSX: number of passed-in dependencies (" << arg.size() << ") should not exceed the number of inputs of the function (" << getNumInputs() << ").");
  vector<SXMatrix> arg2 = arg;
  arg2.resize(getNumInputs());
  for(int iind=0; iind<arg.size(); ++iind){
    // If sparsities do not match, we need to map the nonzeros onto the new pattern
    if(!(arg2[iind].sparsity()==input(iind).sparsity())){
      arg2[iind] = project(arg2[iind],input(iind).sparsity());
    }
  }

  // Create result vector with correct sparsity for the result
  vector<SXMatrix> res(getNumOutputs());
  for(int i=0; i<res.size(); ++i){
    res[i] = SXMatrix(output(i).sparsity());
  }
  
  // No sensitivities
  vector<vector<SXMatrix> > dummy;
  
  // Evaluate the algorithm
  (*this)->eval(arg2,res,dummy,dummy,dummy,dummy,false);
  
  // Return the result
  return res;
}

vector<MX> FX::evalMX(const vector<MX>& arg){
  vector<MX> res;
  vector<vector<MX> > dummy;
  (*this)->evalMX(arg,res,dummy,dummy,dummy,dummy,false);
  return res;
}

void FX::evalSX(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
		       const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
		       const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
		       bool output_given){
  (*this)->evalSX(arg,res,fseed,fsens,aseed,asens,output_given);
}

void FX::evalMX(const std::vector<MX>& arg, std::vector<MX>& res, 
		       const std::vector<std::vector<MX> >& fseed, std::vector<std::vector<MX> >& fsens, 
		       const std::vector<std::vector<MX> >& aseed, std::vector<std::vector<MX> >& asens,
		       bool output_given){
  (*this)->evalMX(arg,res,fseed,fsens,aseed,asens,output_given);
}
                        
void FX::eval(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
		     const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
		     const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
		     bool output_given){
  (*this)->eval(arg,res,fseed,fsens,aseed,asens,output_given);
}

void FX::eval(const std::vector<MX>& arg, std::vector<MX>& res, 
		     const std::vector<std::vector<MX> >& fseed, std::vector<std::vector<MX> >& fsens, 
		     const std::vector<std::vector<MX> >& aseed, std::vector<std::vector<MX> >& asens,
		     bool output_given){
  (*this)->eval(arg,res,fseed,fsens,aseed,asens,output_given);
}

void FX::spEvaluate(bool fwd){
  (*this)->spEvaluate(fwd);
}

bool FX::spCanEvaluate(bool fwd){
  return (*this)->spCanEvaluate(fwd);
}

void FX::spInit(bool fwd){
    (*this)->spInit(fwd);
}

FX FX::derivative(int nfwd, int nadj){
  return (*this)->derivative(nfwd,nadj);
}


} // namespace CasADi
