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

#include "sx_function_internal.hpp"
#include <cassert>
#include <limits>
#include <stack>
#include <deque>
#include <fstream>
#include <sstream>
#include "../stl_vector_tools.hpp"
#include "../sx/sx_tools.hpp"
#include "../sx/sx_node.hpp"
#include "../casadi_types.hpp"
#include "../matrix/crs_sparsity_internal.hpp"

#ifdef WITH_LLVM
#include "llvm/DerivedTypes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/IRBuilder.h"

llvm::IRBuilder<> builder(llvm::getGlobalContext());
#endif // WITH_LLVM

namespace CasADi{

using namespace std;


SXFunctionInternal::SXFunctionInternal(const vector<SXMatrix >& inputv, const vector<SXMatrix >& outputv) : 
  XFunctionInternal<SXFunction,SXFunctionInternal,SXMatrix,SXNode>(inputv,outputv) {
  setOption("name","unnamed_sx_function");
  addOption("just_in_time",OT_BOOLEAN,false,"Just-in-time compilation for numeric evaluation (experimental)");

  // Check for duplicate entries among the input expressions
  bool has_duplicates = false;
  for(vector<SXMatrix >::iterator it = inputv_.begin(); it != inputv_.end(); ++it){
    for(vector<SX>::iterator itc = it->begin(); itc != it->end(); ++itc){
      bool is_duplicate = itc->getTemp()!=0;
      if(is_duplicate){
        cerr << "Duplicate expression: " << *itc << endl;
      }
      has_duplicates = has_duplicates || is_duplicate;
      itc->setTemp(1);
    }
  }
  
  // Reset temporaries
  for(vector<SXMatrix >::iterator it = inputv_.begin(); it != inputv_.end(); ++it){
    for(vector<SX>::iterator itc = it->begin(); itc != it->end(); ++itc){
      itc->setTemp(0);
    }
  }
  
  if(has_duplicates){
    cout << "Input expressions:" << endl;
    for(int iind=0; iind<inputv_.size(); ++iind){
      cout << iind << ": " << inputv_[iind] << endl;
    }
    casadi_error("The input expressions are not independent (or were not reset properly).");
  }
  
  casadi_assert(!outputv_.empty()); // NOTE: Remove?
}

SXFunctionInternal::~SXFunctionInternal(){
}

void SXFunctionInternal::evaluate(int nfdir, int nadir){
  casadi_log("SXFunctionInternal::evaluate(" << nfdir << ", " << nadir<< "):begin  " << getOption("name"));
  // Compiletime optimization for certain common cases
  switch(nfdir){
    case 0:
      evaluateGen1(int_compiletime<0>(),nadir); break;
    case 1:
      evaluateGen1(int_compiletime<1>(),nadir); break;
    case optimized_num_dir:
      evaluateGen1(int_compiletime<optimized_num_dir>(),nadir); break;
    default:
      evaluateGen1(int_runtime(nfdir),nadir); break;
  }
  casadi_log("SXFunctionInternal::evaluate(" << nfdir << ", " << nadir<< "):end " << getOption("name"));
}

template<typename T1>
void SXFunctionInternal::evaluateGen1(T1 nfdir_c, int nadir){
  // Compiletime optimization for certain common cases
  switch(nadir){
    case 0:
      evaluateGen(nfdir_c,int_compiletime<0>()); break;
    case 1:
      evaluateGen(nfdir_c,int_compiletime<1>()); break;
    case optimized_num_dir:
      evaluateGen(nfdir_c,int_compiletime<optimized_num_dir>()); break;
    default:
      evaluateGen(nfdir_c,int_runtime(nadir)); break;
  }
}

template<typename T1, typename T2>
void SXFunctionInternal::evaluateGen(T1 nfdir_c, T2 nadir_c){
  // The following parameters are known either at runtime or at compiletime
  const int nfdir = nfdir_c.value;
  const int nadir = nadir_c.value;
  
  // NOTE: The implementation of this function is very delicate. Small changes in the class structure
  // can cause large performance losses. For this reason, the preprocessor macros are used below
  if (!free_vars_.empty()) {
    std::stringstream ss;
    repr(ss);
    casadi_error("Cannot evaluate \"" << ss.str() << "\" since variables " << free_vars_ << " are free.");
  }
  
  #ifdef WITH_LLVM
  if(just_in_time_ && nfdir==0 && nadir==0){
    // Evaluate the jitted function
    jitfcn_(getPtr(input_ref_),getPtr(output_ref_));
    return;
  }
  #endif // WITH_LLVM
  
  // Do we need taping?
  const bool taping = nfdir>0 || nadir>0;

  // Evaluate the algorithm
  if(!taping){
    for(vector<AlgEl>::iterator it=algorithm_.begin(); it!=algorithm_.end(); ++it){
      switch(it->op){
        // Start by adding all of the built operations
        CASADI_MATH_FUN_BUILTIN(work_[it->arg.i[0]],work_[it->arg.i[1]],work_[it->res])
        
        // Constant
        case OP_CONST: work_[it->res] = it->arg.d; break;
        
        // Load function input to work vector
        case OP_INPUT: work_[it->res] = inputNoCheck(it->arg.i[0]).data()[it->arg.i[1]]; break;
        
        // Get function output from work vector
        case OP_OUTPUT: outputNoCheck(it->res).data()[it->arg.i[1]] = work_[it->arg.i[0]]; break;
      }
    }
  } else {
    vector<TapeEl<double> >::iterator it1 = pdwork_.begin();
    for(vector<AlgEl>::iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
      switch(it->op){
        // Start by adding all of the built operations
        CASADI_MATH_DERF_BUILTIN(work_[it->arg.i[0]],work_[it->arg.i[1]],work_[it->res],it1++->d)

        // Constant
        case OP_CONST: work_[it->res] = it->arg.d; break;

        // Load function input to work vector
        case OP_INPUT: work_[it->res] = inputNoCheck(it->arg.i[0]).data()[it->arg.i[1]]; break;
        
        // Get function output from work vector
        case OP_OUTPUT: outputNoCheck(it->res).data()[it->arg.i[1]] = work_[it->arg.i[0]]; break;
      }
    }
  }
  
  // Quick return if no sensitivities
  if(!taping) return;

  // Calculate forward sensitivities
  for(int dir=0; dir<nfdir; ++dir){
    vector<TapeEl<double> >::const_iterator it2 = pdwork_.begin();
    for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
      switch(it->op){
        case OP_CONST:
          work_[it->res] = 0; break;
        case OP_INPUT: 
          work_[it->res] = fwdSeedNoCheck(it->arg.i[0],dir).data()[it->arg.i[1]]; break;
        case OP_OUTPUT: 
          fwdSensNoCheck(it->res,dir).data()[it->arg.i[1]] = work_[it->arg.i[0]]; break;
        default: // Unary or binary operation
          work_[it->res] = it2->d[0] * work_[it->arg.i[0]] + it2->d[1] * work_[it->arg.i[1]]; ++it2; break;
      }
    }
  }
    
  // Calculate adjoint sensitivities
  if(nadir>0) fill(work_.begin(),work_.end(),0);
  for(int dir=0; dir<nadir; ++dir){
    vector<TapeEl<double> >::const_reverse_iterator it2 = pdwork_.rbegin();
    for(vector<AlgEl>::const_reverse_iterator it = algorithm_.rbegin(); it!=algorithm_.rend(); ++it){
      double seed;
      switch(it->op){
        case OP_CONST:
          work_[it->res] = 0;
          break;
        case OP_INPUT:
          adjSensNoCheck(it->arg.i[0],dir).data()[it->arg.i[1]] = work_[it->res];
          work_[it->res] = 0;
          break;
        case OP_OUTPUT:
          work_[it->arg.i[0]] += adjSeedNoCheck(it->res,dir).data()[it->arg.i[1]];
          break;
        default: // Unary or binary operation
          seed = work_[it->res];
          work_[it->res] = 0;
          work_[it->arg.i[0]] += it2->d[0] * seed;
          work_[it->arg.i[1]] += it2->d[1] * seed;
          ++it2;
      }
    }
  }
}

SXMatrix SXFunctionInternal::hess(int iind, int oind){
  casadi_assert_message(output(oind).numel() == 1, "Function must be scalar");
  SXMatrix g = grad(iind,oind);
  makeDense(g);
  if(verbose())  cout << "SXFunctionInternal::hess: calculating gradient done " << endl;

  // Create function
  SXFunction gfcn(inputv_.at(iind),g);
  gfcn.setOption("verbose",getOption("verbose"));
  gfcn.init();
  
  // Calculate jacobian of gradient
  if(verbose()){
    cout << "SXFunctionInternal::hess: calculating Jacobian " << endl;
  }
  SXMatrix ret = gfcn.jac(0,0,false,true);
  if(verbose()){
    cout << "SXFunctionInternal::hess: calculating Jacobian done" << endl;
  }
  
  // Return jacobian of the gradient
  return ret;
}

bool SXFunctionInternal::isSmooth() const{
  assertInit();
  
  // Go through all nodes and check if any node is non-smooth
  for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
    if(!operation_checker<SmoothChecker>(it->op)){
      return false;
    }
  }
  return true;
}

void SXFunctionInternal::print(ostream &stream) const{
 FXInternal::print(stream);

 // Quick return if not initialized
 if(!isInit()){
   stream << "Function not initialized" << endl;
   return;
 }
 
 // If JIT, dump LLVM IR
  #ifdef WITH_LLVM
  if(just_in_time_){
    jit_module_->dump();
    return;
  }
  #endif // WITH_LLVM
  
  // Iterator to free variables
  vector<SX>::const_iterator p_it = free_vars_.begin();
  
  // Normal, interpreted output
  for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
    if(it->op==OP_OUTPUT){
      stream << "output[" << it->res << "][" << it->arg.i[1] << "] = @" << it->arg.i[0];
    } else {
      stream << "@" << it->res << " = ";
      if(it->op==OP_INPUT){
        stream << "input[" << it->arg.i[0] << "][" << it->arg.i[1] << "]";
      } else {
        if(it->op==OP_CONST){
          stream << it->arg.d;
        } else if(it->op==OP_PARAMETER){
          stream << *p_it++;
        } else {
          int ndep = casadi_math<double>::ndeps(it->op);
          casadi_math<double>::printPre(it->op,stream);
          for(int c=0; c<ndep; ++c){
            if(c==1) casadi_math<double>::printSep(it->op,stream);
            stream << "@" << it->arg.i[c];
          }
          casadi_math<double>::printPost(it->op,stream);
        }
      }
    }
    stream << ";" << endl;
  }
}

void SXFunctionInternal::printVector(std::ostream &cfile, const std::string& name, const vector<int>& v){
  cfile << "int " << name << "[] = {";
  for(int i=0; i<v.size(); ++i){
    if(i!=0) cfile << ",";
    cfile << v[i];
  }
  cfile << "};" << endl;
}

void SXFunctionInternal::generateCode(const string& src_name){
  assertInit();
  
  // Make sure that there are no free variables
  if (!free_vars_.empty()) {
    casadi_error("Code generation is not possible since variables " << free_vars_ << " are free.");
  }
  
   // Output
  if(verbose()){
    cout << "Generating: " << src_name << " (" << algorithm_.size() << " elementary operations)" << endl;
  }
  // Create the c source file
  ofstream cfile;
  cfile.open (src_name.c_str());
  cfile.precision(numeric_limits<double>::digits10+2);
  cfile << scientific; // This is really only to force a decimal dot, would be better if it can be avoided
  
  // Print header
  cfile << "/* This function was automatically generated by CasADi */" << endl;
  cfile << "#include <math.h>" << endl << endl;
  
  // Space saving macro
  cfile << "#define d double" << endl << endl;

  // Number of inputs/outputs
  int n_i = input_.size();
  int n_o = output_.size();
  int n_io = n_i + n_o;

  // Dimensions
  cfile << "int n_in_ = " << n_i << ";" << endl;
  cfile << "int n_out_ = " << n_o << ";" << endl;

  // Number of rows and columns
  vector<int> nrow(n_io), ncol(n_io);
  for(int i=0; i<n_i; ++i){
    nrow[i] = input(i).size1();
    ncol[i] = input(i).size2();
  }
  for(int i=0; i<n_o; ++i){
    nrow[i+n_i] = output(i).size1();
    ncol[i+n_i] = output(i).size2();
  }
  
  // Print to file
  printVector(cfile,"nrow_",nrow);
  printVector(cfile,"ncol_",ncol);
  
  // Print row offsets
  for(int i=0; i<n_io; ++i){
    stringstream name;
    name << "rowind_" << i << "_";
    const vector<int>& rowind = i<n_i ? input(i).rowind() : output(i-n_i).rowind();
    printVector(cfile,name.str(),rowind);
  }
  
  // Array of pointers to the arrays above
  cfile << "int *rowind_[] = {";
  for(int i=0; i<n_io; ++i){
    if(i!=0) cfile << ",";
    cfile << "rowind_" << i << "_"; 
  }
  cfile << "};" << endl;
  
  // Print columns
  for(int i=0; i<n_io; ++i){
    stringstream name;
    name << "col_" << i << "_";
    const vector<int>& col = i<n_i ? input(i).col() : output(i-n_i).col();
    printVector(cfile,name.str(),col);
  }
  
  // Array of pointers to the arrays above
  cfile << "int *col_[] = {";
  for(int i=0; i<n_io; ++i){
    if(i!=0) cfile << ",";
    cfile << "col_" << i << "_"; 
  }
  cfile << "};" << endl << endl;
  
  // Function to get dimensions
  cfile << "int init(int *n_in, int *n_out){" << endl;
  cfile << "  *n_in = n_in_;" << endl;
  cfile << "  *n_out = n_out_;" << endl;
  cfile << "  return 0;" << endl;
  cfile << "}" << endl << endl;

  // Input sizes
  cfile << "int getSparsity(int i, int *nrow, int *ncol, int **rowind, int **col){" << endl;
  cfile << "  *nrow = nrow_[i];" << endl;
  cfile << "  *ncol = ncol_[i];" << endl;
  cfile << "  *rowind = rowind_[i];" << endl;
  cfile << "  *col = col_[i];" << endl;
  cfile << "  return 0;" << endl;
  cfile << "}" << endl << endl;

  // The sign function
  cfile << "double sign(double x){ return x<0 ? -1 : x>0 ? 1 : x;}" << endl << endl;
  
  // Evaluate function
  cfile << "int evaluate(const double** x, double** r){" << endl;

  // Which variables have been declared
  vector<bool> declared(work_.size(),false);
 
  // Run the algorithm
  for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
   if(it->op==OP_OUTPUT){
     cfile << "r[" << it->res << "][" << it->arg.i[1] << "]=" << "a" << it->arg.i[0];
   } else {
     // Declare result if not already declared
     if(!declared[it->res]){
       cfile << "d ";
       declared[it->res]=true;
     }
     
     // Where to store the result
     cfile << "a" << it->res << "=";
    
     // What to store
     if(it->op==OP_CONST){
       cfile << it->arg.d;
     } else if(it->op==OP_INPUT){
       cfile << "x[" << it->arg.i[0] << "][" << it->arg.i[1] << "]";
     } else {
       int ndep = casadi_math<double>::ndeps(it->op);
       casadi_math<double>::printPre(it->op,cfile);
       for(int c=0; c<ndep; ++c){
         if(c==1) casadi_math<double>::printSep(it->op,cfile);
         cfile << "a" << it->arg.i[c];
       }
       casadi_math<double>::printPost(it->op,cfile);
     }
   }
   cfile  << ";" << endl;
 }

  cfile << "return 0;" << endl;
  cfile << "}" << endl << endl;
  
  // Close the results file
  cfile.close();
}

void SXFunctionInternal::init(){
  
  // Call the init function of the base class
  XFunctionInternal<SXFunction,SXFunctionInternal,SXMatrix,SXNode>::init();
  
  // Stack used to sort the computational graph
  stack<SXNode*> s;

  // All nodes
  vector<SXNode*> nodes;

  // Add the list of nodes
  int ind=0;
  for(vector<SXMatrix >::iterator it = outputv_.begin(); it != outputv_.end(); ++it, ++ind){
    int nz=0;
    for(vector<SX>::iterator itc = it->begin(); itc != it->end(); ++itc, ++nz){
      // Add outputs to the list
      s.push(itc->get());
      sort_depth_first(s,nodes);
      
      // A null pointer means an output instruction
      nodes.push_back(static_cast<SXNode*>(0));
    }
  }
  
  // Make sure that all inputs have been added also // TODO REMOVE THIS
  for(vector<SXMatrix >::iterator it = inputv_.begin(); it != inputv_.end(); ++it){
    for(vector<SX>::iterator itc = it->begin(); itc != it->end(); ++itc){
      if(!itc->getTemp()){
        nodes.push_back(itc->get());
      }
    }
  }

  // Set the temporary variables to be the corresponding place in the sorted graph
  for(int i=0; i<nodes.size(); ++i){
    if(nodes[i]){
      nodes[i]->temp = i;
    }
  }
    
  // Sort the nodes by type
  constants_.clear();
  operations_.clear();
  for(vector<SXNode*>::iterator it = nodes.begin(); it != nodes.end(); ++it){
    SXNode* t = *it;
    if(t){
      if(t->isConstant())
        constants_.push_back(SX::create(t));
      else if(!t->isSymbolic())
        operations_.push_back(SX::create(t));
    }
  }
  
  // Use live variables?
  bool live_variables = getOption("live_variables");

  // Input instructions
  vector<pair<int,SXNode*> > symb_loc;
  
  // Current output and nonzero, start with the first one
  int curr_oind, curr_nz=0;
  for(curr_oind=0; curr_oind<outputv_.size(); ++curr_oind){
    if(outputv_[curr_oind].size()!=0){
      break;
    }
  }
  
  // Count the number of times each node is used
  vector<int> refcount(nodes.size(),0);
  
  // Get the sequence of instructions for the virtual machine
  algorithm_.resize(0);
  algorithm_.reserve(nodes.size());
  for(vector<SXNode*>::iterator it=nodes.begin(); it!=nodes.end(); ++it){
    // Current node
    SXNode* n = *it;
 
    // New element in the algorithm
    AlgEl ae;

    // Get operation
    ae.op = n==0 ? OP_OUTPUT : n->getOp();
    
    // Get instruction
    switch(ae.op){
      case OP_CONST: // constant
        ae.arg.d = n->getValue();
        ae.res = n->temp;
        break;
      case OP_PARAMETER: // a parameter or input
        symb_loc.push_back(make_pair(algorithm_.size(),n));
        ae.res = n->temp;
        break;
      case OP_OUTPUT: // output instruction
        ae.res = curr_oind;
        ae.arg.i[0] = outputv_[curr_oind].at(curr_nz)->temp;
        ae.arg.i[1] = curr_nz;
        
        // Go to the next nonzero
        curr_nz++;
        if(curr_nz>=outputv_[curr_oind].size()){
          curr_nz=0;
          curr_oind++;
          for(; curr_oind<outputv_.size(); ++curr_oind){
            if(outputv_[curr_oind].size()!=0){
              break;
            }
          }
        }
        break;
      default:       // Unary or binary operation
        ae.res = n->temp;
        ae.arg.i[0] = n->dep(0).get()->temp;
        ae.arg.i[1] = n->dep(1).get()->temp;
    }
    
    // Number of dependencies
    int ndeps = casadi_math<double>::ndeps(ae.op);
    
    // Increase count of dependencies
    for(int c=0; c<ndeps; ++c)
      refcount[ae.arg.i[c]]++;
    
    // Add to algorithm
    algorithm_.push_back(ae);
  }
  
  // Place in the work vector for each of the nodes in the tree (overwrites the reference counter)
  vector<int> place(nodes.size());
  
  // Stack with unused elements in the work vector
  stack<int> unused;
  
  // Work vector size
  int worksize = 0;
  
  // Find a place in the work vector for the operation
  for(vector<AlgEl>::iterator it=algorithm_.begin(); it!=algorithm_.end(); ++it){
    
    // Number of dependencies
    int ndeps = casadi_math<double>::ndeps(it->op);
  
    // decrease reference count of children
    for(int c=ndeps-1; c>=0; --c){ // reverse order so that the first argument will end up at the top of the stack
      int ch_ind = it->arg.i[c];
      int remaining = --refcount[ch_ind];
      if(remaining==0) unused.push(place[ch_ind]);
    }
    
    // Find a place to store the variable
    if(it->op!=OP_OUTPUT){
      if(live_variables && !unused.empty()){
        // Try to reuse a variable from the stack if possible (last in, first out)
        it->res = place[it->res] = unused.top();
        unused.pop();
      } else {
        // Allocate a new variable
        it->res = place[it->res] = worksize++;
      }
    }
    
    // Save the location of the children
    for(int c=0; c<ndeps; ++c){
      it->arg.i[c] = place[it->arg.i[c]];
    }
    
    // If binary, make sure that the second argument is the same as the first one (in order to treat all operations as binary) NOTE: ugly
    if(ndeps==1 && it->op!=OP_OUTPUT){
      it->arg.i[1] = it->arg.i[0];
    }
  }
  
  if(verbose()){
    if(live_variables){
      cout << "Using live variables: work array is " <<  worksize << " instead of " << nodes.size() << endl;
    } else {
      cout << "Live variables disabled." << endl;
    }
  }
  
  // Allocate work vectors (symbolic/numeric)
  work_.resize(worksize,numeric_limits<double>::quiet_NaN());
  s_work_.resize(worksize);
      
  // Work vector for partial derivatives
  pdwork_.resize(operations_.size());
  
  // Reset the temporary variables
  for(int i=0; i<nodes.size(); ++i){
    if(nodes[i]){
      nodes[i]->temp = 0;
    }
  }
  
  // Now mark each input's place in the algorithm
  for(vector<pair<int,SXNode*> >::const_iterator it=symb_loc.begin(); it!=symb_loc.end(); ++it){
    it->second->temp = it->first+1;
  }
  
  // Add input instructions
  for(int ind=0; ind<inputv_.size(); ++ind){
    int nz=0;
    for(vector<SX>::iterator itc = inputv_[ind].begin(); itc != inputv_[ind].end(); ++itc, ++nz){
      int i = itc->getTemp()-1;
      if(i>=0){
        // Mark as input
        algorithm_[i].op = OP_INPUT;
        
        // Location of the input
        algorithm_[i].arg.i[0] = ind;
        algorithm_[i].arg.i[1] = nz;
        
        // Mark input as read
        itc->setTemp(0);
      }
    }
  }
  
  // Locate free variables
  free_vars_.clear();
  for(vector<pair<int,SXNode*> >::const_iterator it=symb_loc.begin(); it!=symb_loc.end(); ++it){
    if(it->second->temp!=0){
      // Save to list of free parameters
      free_vars_.push_back(SX::create(it->second));
      
      // Remove marker
      it->second->temp=0;
    }
  }
  
  // Allocate memory for directional derivatives
  SXFunctionInternal::updateNumSens(false);
  
  // Initialize just-in-time compilation
  just_in_time_ = getOption("just_in_time");
  if(just_in_time_){
    
    // Make sure that there are no parameters
    if (!free_vars_.empty()) {
      std::stringstream ss;
      repr(ss);
      casadi_error("Cannot just-in-time compile \"" << ss.str() << "\" since variables " << free_vars_ << " are free.");
    }
    
    #ifdef WITH_LLVM
    llvm::InitializeNativeTarget();

    // Function name
    stringstream ss;
    ss << "SXFunction: " << this;
    
    // Make the module, which holds all the code.
    jit_module_ = new llvm::Module(ss.str(), llvm::getGlobalContext());

    // Create the JIT.  This takes ownership of the module.
    std::string ErrStr;
    llvm::ExecutionEngine *TheExecutionEngine = llvm::EngineBuilder(jit_module_).setErrorStr(&ErrStr).create();
    casadi_assert(TheExecutionEngine!=0);
    llvm::FunctionPassManager OurFPM(jit_module_);

    // Set up the optimizer pipeline.  Start with registering info about how the
    // target lays out data structures.
    OurFPM.add(new llvm::TargetData(*TheExecutionEngine->getTargetData()));
    
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    OurFPM.add(llvm::createInstructionCombiningPass());
    
    // Reassociate expressions.
    OurFPM.add(llvm::createReassociatePass());
    
    // Eliminate Common SubExpressions.
    OurFPM.add(llvm::createGVNPass());
    
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    OurFPM.add(llvm::createCFGSimplificationPass());
    OurFPM.doInitialization();

    // Single argument
    vector<llvm::Type*> unaryArg(1,llvm::Type::getDoubleTy(llvm::getGlobalContext()));

    // Two arguments
    vector<llvm::Type*> binaryArg(2,llvm::Type::getDoubleTy(llvm::getGlobalContext()));
    
    // Unary operation
    llvm::FunctionType *unaryFun = llvm::FunctionType::get(llvm::Type::getDoubleTy(llvm::getGlobalContext()),unaryArg, false);

    // Binary operation
    llvm::FunctionType *binaryFun = llvm::FunctionType::get(llvm::Type::getDoubleTy(llvm::getGlobalContext()),binaryArg, false);

    // Declare all the CasADi built-in functions
    vector<llvm::Function*> builtins(NUM_BUILT_IN_OPS,0);
    builtins[OP_POW] = builtins[OP_CONSTPOW] = llvm::Function::Create(binaryFun, llvm::Function::ExternalLinkage, "pow", jit_module_);
    builtins[OP_SQRT] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "sqrt", jit_module_);
    builtins[OP_SIN] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "sin", jit_module_);
    builtins[OP_COS] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "cos", jit_module_);
    builtins[OP_TAN] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "tan", jit_module_);
    builtins[OP_ASIN] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "asin", jit_module_);
    builtins[OP_ACOS] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "acos", jit_module_);
    builtins[OP_ATAN] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "atan", jit_module_);
    builtins[OP_FLOOR] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "floor", jit_module_);
    builtins[OP_CEIL] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "ceil", jit_module_);
    builtins[OP_FMIN] = llvm::Function::Create(binaryFun, llvm::Function::ExternalLinkage, "fmin", jit_module_);
    builtins[OP_FMAX] = llvm::Function::Create(binaryFun, llvm::Function::ExternalLinkage, "fmax", jit_module_);
    builtins[OP_SINH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "sinh", jit_module_);
    builtins[OP_COSH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "cosh", jit_module_);
    builtins[OP_TANH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "tanh", jit_module_);
    builtins[OP_ASINH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "asinh", jit_module_);
    builtins[OP_ACOSH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "acosh", jit_module_);
    builtins[OP_ATANH] = llvm::Function::Create(unaryFun, llvm::Function::ExternalLinkage, "atanh", jit_module_);

    // Void type
    llvm::Type* void_t = llvm::Type::getVoidTy(llvm::getGlobalContext());

    // Double type
    llvm::Type* double_t = llvm::Type::getDoubleTy(llvm::getGlobalContext());
    
    // Double pointer type
    llvm::Type* double_ptr_t = llvm::Type::getDoublePtrTy(llvm::getGlobalContext());

    // Double pointer pointer type
    llvm::Type* double_ptr_ptr_t = llvm::PointerType::getUnqual(double_ptr_t);

    // A normal 32-bit integer
    llvm::IntegerType *int32Ty = llvm::IntegerType::get(llvm::getGlobalContext(), 32);
    
    // Two arguments in and two references
    vector<llvm::Type*> genArg(2);
    genArg[0] = double_ptr_ptr_t;
    genArg[1] = double_ptr_ptr_t;
    
    // More generic operation, return by reference
    llvm::FunctionType *genFun = llvm::FunctionType::get(void_t,genArg, false);

    // Declare my function
    jit_function_ = llvm::Function::Create(genFun, llvm::Function::ExternalLinkage, ss.str(), jit_module_);

    // Create a new basic block to start insertion into.
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", jit_function_);
    builder.SetInsertPoint(BB);

    // Set names for all arguments.
    llvm::Function::arg_iterator AI = jit_function_->arg_begin();
    AI->setName("x");  llvm::Value *x_ptr = AI++;
    AI->setName("r");  llvm::Value *r_ptr = AI++;

    // Allocate work vector
    vector<llvm::Value*> jwork(work_.size());

    // Input vectors
    vector<llvm::Value*> input_v(getNumInputs());
    for(int ind=0; ind<input_v.size(); ++ind){
      llvm::Value *ind_v = llvm::ConstantInt::get(int32Ty, ind);
      input_v[ind] = builder.CreateLoad(builder.CreateGEP(x_ptr,ind_v));
    }
    
    // Output vectors
    vector<llvm::Value*> output_v(getNumOutputs());
    for(int ind=0; ind<output_v.size(); ++ind){
      llvm::Value *ind_v = llvm::ConstantInt::get(int32Ty, ind);
      output_v[ind] = builder.CreateLoad(builder.CreateGEP(r_ptr,ind_v));
    }
        
    // Build up the LLVM expression graphs
    for(vector<AlgEl>::iterator it=algorithm_.begin(); it!=algorithm_.end(); ++it){
      // Argument of the operation
      vector<llvm::Value*> oarg(casadi_math<double>::ndeps(it->op));
      for(int d=0; d<oarg.size(); ++d){
        oarg[d] = jwork[it->arg.i[d]];
      }
      
      if(it->op==OP_INPUT){
        llvm::Value *k_v = llvm::ConstantInt::get(int32Ty, it->arg.i[1]);
        jwork[it->res] = builder.CreateLoad(builder.CreateGEP(input_v[it->arg.i[0]],k_v));
      } else if(it->op==OP_OUTPUT){
        llvm::Value *k_v = llvm::ConstantInt::get(int32Ty, it->arg.i[1]);
        builder.CreateStore(oarg[0],builder.CreateGEP(output_v[it->res],k_v));
      } else {
        // Result
        llvm::Value* res = 0;
        
        switch(it->op){
          case OP_ADD:         res = builder.CreateFAdd(oarg[0],oarg[1]); break;
          case OP_SUB:         res = builder.CreateFSub(oarg[0],oarg[1]); break;
          case OP_MUL:         res = builder.CreateFMul(oarg[0],oarg[1]); break;
          case OP_DIV:         res = builder.CreateFDiv(oarg[0],oarg[1]); break;
          case OP_NEG:         res = builder.CreateFNeg(oarg[0]);         break;
          case OP_CONST:
            res = llvm::ConstantFP::get(llvm::getGlobalContext(), llvm::APFloat(it->arg.d));
            break;
          default:
            casadi_assert_message(builtins[it->op]!=0, "No way to treat: " << it->op);
            res = builder.CreateCall(builtins[it->op], oarg);
        }
        
        // Save to work vector
        jwork[it->res] = res;
      }
    }
    
    // Finish off the function.
    builder.CreateRetVoid();

    // Validate the generated code, checking for consistency.
    verifyFunction(*jit_function_);

    // Optimize the function.
    OurFPM.run(*jit_function_);

    // JIT the function
    jitfcn_ = evaluateFcn(intptr_t(TheExecutionEngine->getPointerToFunction(jit_function_)));

    // Allocate references to input nonzeros
    input_ref_.resize(getNumInputs());
    for(int ind=0; ind<input_ref_.size(); ++ind){
      input_ref_[ind] = getPtr(input(ind).data());
    }
        
    // Allocate references to output nonzeros
    output_ref_.resize(getNumOutputs());
    for(int ind=0; ind<output_ref_.size(); ++ind){
      output_ref_[ind] = getPtr(output(ind).data());
    }
    
    #else // WITH_LLVM
    casadi_error("Option \"just_in_time\" true requires CasADi to have been compiled with WITH_LLVM=ON");
    #endif //WITH_LLVM
  }
  
  // Print
  if(verbose()){
    cout << "SXFunctionInternal::init Initialized " << getOption("name") << " (" << algorithm_.size() << " elementary operations)" << endl;
  }
}

void SXFunctionInternal::updateNumSens(bool recursive){
  // Call the base class if needed
  if(recursive) XFunctionInternal<SXFunction,SXFunctionInternal,SXMatrix,SXNode>::updateNumSens(recursive);
}

void SXFunctionInternal::evalSX(const vector<SXMatrix>& arg, vector<SXMatrix>& res, 
                                const vector<vector<SXMatrix> >& fseed, vector<vector<SXMatrix> >& fsens, 
                                const vector<vector<SXMatrix> >& aseed, vector<vector<SXMatrix> >& asens,
                                bool output_given){

  if(verbose()) cout << "SXFunctionInternal::evalSX begin" << endl;
  
  // Assert number of inputs
  casadi_assert_message(inputv_.size() == arg.size(),"Wrong number of inputs. Expecting " << inputv_.size() << ", got " << arg.size());

  // Assert number of forward seeds
  int nfdir = fsens.size();
  for(int dir=0; dir<nfdir; ++dir){
    casadi_assert_message(inputv_.size() == fseed[dir].size(),"Wrong number of forward seeds in direction " << dir << ". Expecting " << inputv_.size() << ", got " << fseed[dir].size());
  }
  
  // Assert number of adjoint seeds
  int nadir = aseed.size();
  for(int dir=0; dir<nadir; ++dir){
    casadi_assert_message(outputv_.size() == aseed[dir].size(),"Wrong number of adjoint seeds in direction " << dir << ". Expecting " << outputv_.size() << ", got " << aseed[dir].size());
  }
  
  // Check if input sparsity pattern match (quick if sparsity matches)
  bool sparsity_matches = true;
  for(int i=0; i<inputv_.size() && sparsity_matches; ++i){
    sparsity_matches = arg[i].sparsity()==inputv_[i].sparsity();
  }
  if(!sparsity_matches){
    casadi_assert_message(!output_given,"Inconsistent arguments.");
    vector<SXMatrix> arg_new(arg.size());
    for(int i=0; i<arg.size(); ++i){
      try{
        arg_new[i] = SXMatrix(inputv_[i].sparsity());
        arg_new[i].set(arg[i]);
      } catch(exception& ex){
        stringstream ss;
        ss << "SXFunctionInternal::evalSX: Failed to set input " << i << ": " << ex.what();
        throw CasadiException(ss.str());
      }
    }
    evalSX(arg_new,res,fseed,fsens,aseed,asens,output_given);
    return;
  }
  
  // Check if forward seed sparsity pattern match (quick if sparsity matches)
  for(int dir=0; dir<nfdir && sparsity_matches; ++dir){
    for(int i=0; i<inputv_.size() && sparsity_matches; ++i){
      sparsity_matches = fseed[dir][i].sparsity()==inputv_[i].sparsity();
    }
  }
  if(!sparsity_matches){
    vector<vector<SXMatrix> > fseed_new(nfdir);
    for(int dir=0; dir<nfdir; ++dir){
      fseed_new[dir].resize(inputv_.size());
      for(int i=0; i<inputv_.size(); ++i){
        try{
          fseed_new[dir][i] = SXMatrix(inputv_[i].sparsity());
          fseed_new[dir][i].set(fseed[dir][i]);
        } catch(exception& ex){
          stringstream ss;
          ss << "SXFunctionInternal::evalSX: Failed to set forward seed  " << i << ", direction " << dir << ": " << ex.what();
          throw CasadiException(ss.str());
        }
      }
    }
    evalSX(arg,res,fseed_new,fsens,aseed,asens,output_given);
    return;
  }
  
  // Check if adjoint seed sparsity pattern match (quick if sparsity matches)
  for(int dir=0; dir<nadir && sparsity_matches; ++dir){
    for(int i=0; i<outputv_.size() && sparsity_matches; ++i){
      sparsity_matches = aseed[dir][i].sparsity()==outputv_[i].sparsity();
    }
  }
  if(!sparsity_matches){
    vector<vector<SXMatrix> > aseed_new(nadir);
    for(int dir=0; dir<nadir; ++dir){
      aseed_new[dir].resize(outputv_.size());
      for(int i=0; i<outputv_.size(); ++i){
        try{
          aseed_new[dir][i] = SXMatrix(outputv_[i].sparsity());
          aseed_new[dir][i].set(aseed[dir][i]);
        } catch(exception& ex){
          stringstream ss;
          ss << "SXFunctionInternal::evalSX: Failed to set adjoint seed  " << i << ", direction " << dir << ": " << ex.what();
          throw CasadiException(ss.str());
        }
      }
    }
    evalSX(arg,res,fseed,fsens,aseed_new,asens,output_given);
    return;
  }
    
  // Resize (if needed) the number of outputs and make sure that the sparsity pattern is correct (cheap if already ok)
  res.resize(outputv_.size());
  for(int i=0; i<outputv_.size(); ++i){
    if(res[i].sparsity()!=outputv_[i].sparsity()){
      res[i] = SXMatrix(outputv_[i].sparsity());
    }
  }
  
  // Resize (if needed) the number of forward sensitivities and make sure that the sparsity pattern is correct (cheap if already ok)
  fsens.resize(nfdir);
  for(int dir=0; dir<nfdir; ++dir){
    fsens[dir].resize(outputv_.size());
    for(int i=0; i<outputv_.size(); ++i){
      if(fsens[dir][i].sparsity()!=outputv_[i].sparsity()){
        fsens[dir][i] = SXMatrix(outputv_[i].sparsity());
      }
    }
  }
  
  // Resize (if needed) the number of adjoint sensitivities and make sure that the sparsity pattern is correct (cheap if already ok)
  asens.resize(nadir);
  for(int dir=0; dir<nadir; ++dir){
    asens[dir].resize(inputv_.size());
    for(int i=0; i<inputv_.size(); ++i){
      if(asens[dir][i].sparsity()!=inputv_[i].sparsity()){
        asens[dir][i] = SXMatrix(inputv_[i].sparsity());
      }
    }
  }

  // Do we need taping?
  bool taping = nfdir>0 || nadir>0;
  
  // Iterator to the binary operations
  vector<SX>::const_iterator b_it=operations_.begin();
  
  // Iterator to stack of constants
  vector<SX>::const_iterator c_it = constants_.begin();

  // Iterator to free variables
  vector<SX>::const_iterator p_it = free_vars_.begin();
  
  // Tape
  vector<TapeEl<SX> > s_pdwork;
  vector<TapeEl<SX> >::iterator it1;
  if(taping){
    s_pdwork.resize(operations_.size());
    it1 = s_pdwork.begin();
  }

  // Evaluate algorithm
  if(verbose()) cout << "SXFunctionInternal::evalSX evaluating algorithm forward" << endl;
  for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
    switch(it->op){
      case OP_INPUT:
        s_work_[it->res] = arg[it->arg.i[0]].data()[it->arg.i[1]]; break;
      case OP_OUTPUT:
        res[it->res].data()[it->arg.i[1]] = s_work_[it->arg.i[0]]; 
        break;
      case OP_CONST:
        s_work_[it->res] = *c_it++; 
        break;
      case OP_PARAMETER:
        s_work_[it->res] = *p_it++; break;
      default:
      {
        // Evaluate the function to a temporary value (as it might overwrite the children in the work vector)
        SX f;
        if(output_given){
          f = *b_it++;
        } else {
          switch(it->op){
            CASADI_MATH_FUN_BUILTIN(s_work_[it->arg.i[0]],s_work_[it->arg.i[1]],f)
          }
          
          // If this new expression is identical to the expression used to define the algorithm, then reuse
          const int depth = 2; // NOTE: a higher depth could possibly give more savings
          f.assignIfDuplicate(*b_it++,depth);
        }
        
        // Get the partial derivatives, if requested
        if(taping){
          switch(it->op){
            CASADI_MATH_DER_BUILTIN(s_work_[it->arg.i[0]],s_work_[it->arg.i[1]],f,it1++->d)
          }
        }
        
        // Finally save the function value
        s_work_[it->res] = f;
      }
    }
  }
  
  // Quick return if no sensitivities
  if(!taping) return;

  // Calculate forward sensitivities
  if(verbose()) cout << "SXFunctionInternal::evalSX calculating forward derivatives" << endl;
  for(int dir=0; dir<nfdir; ++dir){
    vector<TapeEl<SX> >::const_iterator it2 = s_pdwork.begin();
    for(vector<AlgEl>::const_iterator it = algorithm_.begin(); it!=algorithm_.end(); ++it){
      switch(it->op){
        case OP_INPUT:
          s_work_[it->res] = fseed[dir][it->arg.i[0]].data()[it->arg.i[1]]; break;
        case OP_OUTPUT:
          fsens[dir][it->res].data()[it->arg.i[1]] = s_work_[it->arg.i[0]]; break;
        case OP_CONST:
        case OP_PARAMETER:
          s_work_[it->res] = 0;
          break;
        CASADI_MATH_BINARY_BUILTIN // Binary operation
          s_work_[it->res] = it2->d[0] * s_work_[it->arg.i[0]] + it2->d[1] * s_work_[it->arg.i[1]]; it2++; break;
        default: // Unary operation
          s_work_[it->res] = it2->d[0] * s_work_[it->arg.i[0]]; it2++; 
      }
    }
  }

  // Calculate adjoint sensitivities
  if(verbose()) cout << "SXFunctionInternal::evalSX calculating adjoint derivatives" << endl;
  if(nadir>0) fill(s_work_.begin(),s_work_.end(),0);
  for(int dir=0; dir<nadir; ++dir){
    vector<TapeEl<SX> >::const_reverse_iterator it2 = s_pdwork.rbegin();
    for(vector<AlgEl>::const_reverse_iterator it = algorithm_.rbegin(); it!=algorithm_.rend(); ++it){
      SX seed;
      switch(it->op){
        case OP_INPUT:
          asens[dir][it->arg.i[0]].data()[it->arg.i[1]] = s_work_[it->res];
          s_work_[it->res] = 0;
          break;
        case OP_OUTPUT:
          s_work_[it->arg.i[0]] += aseed[dir][it->res].data()[it->arg.i[1]];
          break;
        case OP_CONST:
        case OP_PARAMETER:
          s_work_[it->res] = 0;
          break;
        CASADI_MATH_BINARY_BUILTIN // Binary operation
          seed = s_work_[it->res];
          s_work_[it->res] = 0;
          s_work_[it->arg.i[0]] += it2->d[0] * seed;
          s_work_[it->arg.i[1]] += it2->d[1] * seed;
          it2++;
          break;
        default: // Unary operation
          seed = s_work_[it->res];
          s_work_[it->res] = 0;
          s_work_[it->arg.i[0]] += it2->d[0] * seed;
          it2++; 
      }
    }
  }
  if(verbose()) cout << "SXFunctionInternal::evalSX end" << endl;
}

SXFunctionInternal* SXFunctionInternal::clone() const{
  return new SXFunctionInternal(*this);
}


void SXFunctionInternal::clearSymbolic(){
  inputv_.clear();
  outputv_.clear();
  s_work_.clear();
}

void SXFunctionInternal::spInit(bool fwd){
  // We need a work array containing unsigned long rather than doubles. Since the two datatypes have the same size (64 bits)
  // we can save overhead by reusing the double array
  bvec_t *iwork = get_bvec_t(work_);
  if(!fwd) fill_n(iwork,work_.size(),bvec_t(0));
}

void SXFunctionInternal::spEvaluate(bool fwd){
  // Get work array
  bvec_t *iwork = get_bvec_t(work_);

  if(fwd){
    // Propagate sparsity forward
    for(vector<AlgEl>::iterator it=algorithm_.begin(); it!=algorithm_.end(); ++it){
      switch(it->op){
        case OP_CONST:
        case OP_PARAMETER:
          iwork[it->res] = bvec_t(0); break;
        case OP_INPUT:
          iwork[it->res] = reinterpret_cast<bvec_t*>(&inputNoCheck(it->arg.i[0]).front())[it->arg.i[1]]; break;
        case OP_OUTPUT:
          reinterpret_cast<bvec_t*>(&outputNoCheck(it->res).front())[it->arg.i[1]] = iwork[it->arg.i[0]]; break;
        default: // Unary or binary operation
          iwork[it->res] = iwork[it->arg.i[0]] | iwork[it->arg.i[1]]; break;
      }
    }
        
  } else { // Backward propagation

    // Propagate sparsity backward
    for(vector<AlgEl>::reverse_iterator it=algorithm_.rbegin(); it!=algorithm_.rend(); ++it){
      // Temp seed
      bvec_t seed;
      
      // Propagate seeds
      switch(it->op){
        case OP_CONST:
        case OP_PARAMETER:
          iwork[it->res] = 0;
          break;
        case OP_INPUT:
          reinterpret_cast<bvec_t*>(&inputNoCheck(it->arg.i[0]).front())[it->arg.i[1]] = iwork[it->res];
          iwork[it->res] = 0;
          break;
        case OP_OUTPUT:
          iwork[it->arg.i[0]] |= reinterpret_cast<bvec_t*>(&outputNoCheck(it->res).front())[it->arg.i[1]];
          break;
        default: // Unary or binary operation
          seed = iwork[it->res];
          iwork[it->res] = 0;
          iwork[it->arg.i[0]] |= seed;
          iwork[it->arg.i[1]] |= seed; 
      }
    }
  }
}

  FX SXFunctionInternal::getFullJacobian(){
    // Get the nonzeros of each input
    vector<SXMatrix> argv = inputv_;
    for(int ind=0; ind<argv.size(); ++ind){
      if(argv[ind].size2()!=1 || !argv[ind].dense()){
	argv[ind] = argv[ind][Slice()];
      }
    }

    // Concatenate to get all output nonzeros
    SXMatrix arg = vertcat(argv);
    casadi_assert(arg.size() == getNumScalarInputs());

    // Get the nonzeros of each output
    vector<SXMatrix> resv = outputv_;
    for(int ind=0; ind<resv.size(); ++ind){
      if(resv[ind].size2()!=1 || !resv[ind].dense()){
	resv[ind] = resv[ind][Slice()];
      }
    }

    // Concatenate to get all output nonzeros
    SXMatrix res = vertcat(resv);
    casadi_assert(res.size() == getNumScalarOutputs());

    // Form function of all inputs nonzeros to all output nonzeros and return Jacobian of this
    FX f = SXFunction(arg,res);
    f.init();
    return f.jacobian(0,0,false,false);
  }

} // namespace CasADi

