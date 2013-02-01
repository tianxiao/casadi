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
#include <typeinfo> 
#include "../stl_vector_tools.hpp"
#include "mx_function.hpp"
#include "../matrix/matrix_tools.hpp"
#include "../sx/sx_tools.hpp"
#include "../mx/mx_tools.hpp"
#include "../matrix/sparsity_tools.hpp"

using namespace std;

namespace CasADi{
  
FXInternal::FXInternal(){
  setOption("name","unnamed_function"); // name of the function
  addOption("sparse",                   OT_BOOLEAN,             true,           "function is sparse");
  addOption("number_of_fwd_dir",        OT_INTEGER,             1,              "number of forward derivatives to be calculated simultanously");
  addOption("number_of_adj_dir",        OT_INTEGER,             1,              "number of adjoint derivatives to be calculated simultanously");
  addOption("max_number_of_fwd_dir",    OT_INTEGER,             optimized_num_dir,  "Allow \"number_of_fwd_dir\" to grow until it reaches this number");
  addOption("max_number_of_adj_dir",    OT_INTEGER,             optimized_num_dir,  "Allow \"number_of_adj_dir\" to grow until it reaches this number");
  addOption("verbose",                  OT_BOOLEAN,             false,          "verbose evaluation -- for debugging");
  addOption("store_jacobians",          OT_BOOLEAN,             false,          "keep references to generated Jacobians in order to avoid generating identical Jacobians multiple times");
  addOption("numeric_jacobian",         OT_BOOLEAN,             false,          "Calculate Jacobians numerically (using directional derivatives) rather than with the built-in method");
  addOption("numeric_hessian",          OT_BOOLEAN,             false,          "Calculate Hessians numerically (using directional derivatives) rather than with the built-in method");
  addOption("ad_mode",                  OT_STRING,              "automatic",    "How to calculate the Jacobians: \"forward\" (only forward mode) \"reverse\" (only adjoint mode) or \"automatic\" (a heuristic decides which is more appropriate)","forward|reverse|automatic");
  addOption("jacobian_generator",       OT_JACOBIANGENERATOR,   GenericType(),  "Function pointer that returns a Jacobian function given a set of desired Jacobian blocks, overrides internal routines");
  addOption("sparsity_generator",       OT_SPARSITYGENERATOR,   GenericType(),  "Function that provides sparsity for a given input output block, overrides internal routines");
  addOption("user_data",                OT_VOIDPTR,             GenericType(),  "A user-defined field that can be used to identify the function or pass additional information");
  addOption("monitor",      OT_STRINGVECTOR, GenericType(),  "Monitors to be activated","inputs|outputs");
  addOption("regularity_check",         OT_BOOLEAN,             true,          "Throw exceptions when NaN or Inf appears during evaluation");
  addOption("gather_stats",             OT_BOOLEAN,             false,         "Flag to indicate wether statistics must be gathered");
  
  verbose_ = false;
  jacgen_ = 0;
  spgen_ = 0;
  user_data_ = 0;
  monitor_inputs_ = false;
  monitor_outputs_ = false;
  
  inputScheme  = SCHEME_unknown;
  outputScheme = SCHEME_unknown;
}



FXInternal::~FXInternal(){
}

void FXInternal::init(){
  verbose_ = getOption("verbose");
  regularity_check_ = getOption("regularity_check");
  bool store_jacobians = getOption("store_jacobians");
  casadi_assert_warning(!store_jacobians,"Option \"store_jacobians\" has been deprecated. Jacobians are now always cached.");
  
  // Allocate data for sensitivities (only the method in this class)
  FXInternal::updateNumSens(false);
  
  // Resize the matrix that holds the sparsity of the Jacobian blocks
  jac_sparsity_compact_.resize(getNumInputs(),vector<CRSSparsity>(getNumOutputs()));
  jac_sparsity_.resize(getNumInputs(),vector<CRSSparsity>(getNumOutputs()));

  // Get the Jacobian generator function, if any
  if(hasSetOption("jacobian_generator")){
    jacgen_ = getOption("jacobian_generator");
  }
  
  // Get the sparsity detector function, if any
  if(hasSetOption("sparsity_generator")){
    spgen_ = getOption("sparsity_generator");
  }

  if(hasSetOption("user_data")){
    user_data_ = getOption("user_data").toVoidPointer();
  }
  
  // Pass monitors
  if(hasSetOption("monitor")){
    const std::vector<std::string>& monitors = getOption("monitor");
    for (std::vector<std::string>::const_iterator it=monitors.begin();it!=monitors.end();it++) {
      monitors_.insert(*it);
    }
  }
  
  monitor_inputs_ = monitored("inputs");
  monitor_outputs_ = monitored("outputs");
  
  gather_stats_ = getOption("gather_stats");

  // Mark the function as initialized
  is_init_ = true;
}

void FXInternal::updateNumSens(bool recursive){
  // Get the new number
  nfdir_ = getOption("number_of_fwd_dir");
  nadir_ = getOption("number_of_adj_dir");
  
  // Warn if the number exceeds the maximum
  casadi_assert_warning(nfdir_ <= int(getOption("max_number_of_fwd_dir")), "The number of forward directions exceeds the maximum number. Decrease \"number_of_fwd_dir\" or increase \"max_number_of_fwd_dir\"");
  casadi_assert_warning(nadir_ <= int(getOption("max_number_of_adj_dir")), "The number of adjoint directions exceeds the maximum number. Decrease \"number_of_adj_dir\" or increase \"max_number_of_adj_dir\"");
  
  // Allocate memory for the seeds and sensitivities
  for(vector<FunctionIO>::iterator it=input_.begin(); it!=input_.end(); ++it){
    it->dataF.resize(nfdir_,it->data);
    it->dataA.resize(nadir_,it->data);
  }
  for(vector<FunctionIO>::iterator it=output_.begin(); it!=output_.end(); ++it){
    it->dataF.resize(nfdir_,it->data);
    it->dataA.resize(nadir_,it->data);
  }

  // Allocate memory for compression marker
  compressed_fwd_.resize(nfdir_);
  compressed_adj_.resize(nadir_);
}

void FXInternal::requestNumSens(int nfwd, int nadj){
  // Request the number of directions to the number that we would ideally have
  int nfwd_new = std::max(nfwd,std::max(nfdir_,int(getOption("number_of_fwd_dir"))));
  int nadj_new = std::max(nadj,std::max(nadir_,int(getOption("number_of_adj_dir"))));
  
  // Cap at the maximum
  nfwd_new = std::min(nfwd_new,int(getOption("max_number_of_fwd_dir")));
  nadj_new = std::min(nadj_new,int(getOption("max_number_of_adj_dir")));
  
  // Update the number of directions, if needed
  if(nfwd_new>nfdir_ || nadj_new>nadir_){
    setOption("number_of_fwd_dir",nfwd_new);
    setOption("number_of_adj_dir",nadj_new);
    updateNumSens(true);
  }
}

void FXInternal::print(ostream &stream) const{
  if (getNumInputs()==1) {
    stream << " Input: " << input().dimString() << std::endl;
  } else{
    stream << " Inputs (" << getNumInputs() << "):" << std::endl;
    for (int i=0;i<getNumInputs();i++) {
      stream << "  " << i+1 << ". " << input(i).dimString() << std::endl;
    }
  }
  if (getNumOutputs()==1) {
    stream << " Output: " << output().dimString() << std::endl;
  } else {
    stream << " Outputs (" << getNumOutputs() << "):" << std::endl;
    for (int i=0;i<getNumOutputs();i++) {
      stream << "  " << i+1 << ". " << output(i).dimString() << std::endl;
    }
  }
}

void FXInternal::repr(ostream &stream) const{
  stream << "function(\"" << getOption("name") << "\")";
}

FX FXInternal::gradient(int iind, int oind){
  // Assert scalar
  casadi_assert_message(output(oind).scalar(),"Only gradients of scalar functions allowed. Use jacobian instead.");
  
  // Generate gradient function
  FX ret = getGradient(iind,oind);
  
  // Give it a suitable name
  stringstream ss;
  ss << "gradient_" << getOption("name") << "_" << iind << "_" << oind;
  ret.setOption("name",ss.str());
  
  return ret;
}
  
FX FXInternal::hessian(int iind, int oind){
  log("FXInternal::hessian");
  
  // Assert scalar
  casadi_assert_message(output(oind).scalar(),"Only hessians of scalar functions allowed.");
  
  // Generate gradient function
  FX ret = getHessian(iind,oind);
  
  // Give it a suitable name
  stringstream ss;
  ss << "hessian_" << getOption("name") << "_" << iind << "_" << oind;
  ret.setOption("name",ss.str());
  
  return ret;
}
  
FX FXInternal::getGradient(int iind, int oind){
  casadi_error("FXInternal::getGradient: getGradient not defined for class " << typeid(*this).name());
}
  
FX FXInternal::getHessian(int iind, int oind){
  log("FXInternal::getHessian");

  // Create gradient function
  log("FXInternal::getHessian generating gradient");
  FX g = gradient(iind,oind);
  g.setOption("numeric_jacobian",getOption("numeric_hessian"));
  g.setOption("verbose",getOption("verbose"));
  g.init();
  
  // Return the Jacobian of the gradient, exploiting symmetry (the gradient has output index 0)
  log("FXInternal::getHessian generating Jacobian of gradient");
  return g.jacobian(iind,0,false,true);
}
  
void FXInternal::log(const string& msg) const{
  if(verbose()){
    cout << "CasADi log message: " << msg << endl;
  }
}

void FXInternal::log(const string& fcn, const string& msg) const{
  if(verbose()){
    cout << "CasADi log message: In \"" << fcn << "\" --- " << msg << endl;
  }
}

bool FXInternal::verbose() const{
  return verbose_;
}

bool FXInternal::monitored(const string& mod) const{
  return monitors_.count(mod)>0;
}

void FXInternal::setNumInputs(int num_in){
  input_.resize(num_in);
}

void FXInternal::setNumOutputs(int num_out){
  output_.resize(num_out);  
}

const Dictionary & FXInternal::getStats() const {
  return stats_;
}

GenericType FXInternal::getStat(const string & name) const {
  // Locate the statistic
  Dictionary::const_iterator it = stats_.find(name);

  // Check if found
  if(it == stats_.end()){
    casadi_error("Statistic: " << name << " has not been set." << endl <<  "Note: statistcs are only set after an evaluate call");
  }

  return GenericType(it->second);
}

std::vector<MX> FXInternal::symbolicInput() const{
  vector<MX> ret(getNumInputs());
  assertInit();
  for(int i=0; i<ret.size(); ++i){
    stringstream name;
    name << "x_" << i;
    ret[i] = MX(name.str(),input(i).sparsity());
  }
  return ret;
}

std::vector<SXMatrix> FXInternal::symbolicInputSX() const{
  vector<SXMatrix> ret(getNumInputs());
  assertInit();
  for(int i=0; i<ret.size(); ++i){
    stringstream name;
    name << "x_" << i;
    ret[i] = ssym(name.str(),input(i).sparsity());
  }
  return ret;
}

void bvec_toggle(bvec_t* s, int begin, int end,int j) {
  for(int i=begin; i<end; ++i){
    s[i] ^= (bvec_t(1) << j);
  }
}

void bvec_clear(bvec_t* s, int begin, int end) {
  for(int i=begin; i<end; ++i){
    s[i] = 0;
  }
}


void bvec_or(bvec_t* s, bvec_t & r, int begin, int end) {
  r = 0;
  for(int i=begin; i<end; ++i) r |= s[i];
}

CRSSparsity FXInternal::getJacSparsityPlain(int iind, int oind){
// Number of nonzero inputs
    int nz_in = input(iind).size();
    
    // Number of nonzero outputs
    int nz_out = output(oind).size();

    // Number of forward sweeps we must make
    int nsweep_fwd = nz_in/bvec_size;
    if(nz_in%bvec_size>0) nsweep_fwd++;
    
    // Number of adjoint sweeps we must make
    int nsweep_adj = nz_out/bvec_size;
    if(nz_out%bvec_size>0) nsweep_adj++;
    
    // Use forward mode?
    bool use_fwd = spCanEvaluate(true) && nsweep_fwd <= nsweep_adj;
    
    // Override default behavior?
    if(getOption("ad_mode") == "forward"){
      use_fwd = true;
    } else if(getOption("ad_mode") == "reverse"){
      use_fwd = false;
    }
    
    // Reset the virtual machine
    spInit(use_fwd);

    // Clear the forward seeds/adjoint sensitivities
    for(int ind=0; ind<getNumInputs(); ++ind){
      vector<double> &v = inputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }

    // Clear the adjoint seeds/forward sensitivities
    for(int ind=0; ind<getNumOutputs(); ++ind){
      vector<double> &v = outputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }
    
    // Get seeds and sensitivities
    bvec_t* input_v = get_bvec_t(inputNoCheck(iind).data());
    bvec_t* output_v = get_bvec_t(outputNoCheck(oind).data());
    bvec_t* seed_v = use_fwd ? input_v : output_v;
    bvec_t* sens_v = use_fwd ? output_v : input_v;
    
    // Number of sweeps needed
    int nsweep = use_fwd ? nsweep_fwd : nsweep_adj;
    
    // The number of zeros in the seed and sensitivity directions
    int nz_seed = use_fwd ? nz_in  : nz_out;
    int nz_sens = use_fwd ? nz_out : nz_in;

    // Print
    if(verbose()){
      std::cout << "FXInternal::getJacSparsity: using " << (use_fwd ? "forward" : "adjoint") << " mode: ";
      std::cout << nsweep << " sweeps needed for " << nz_seed << " directions" << std::endl;
    }
    
    // Progress
    int progress = -10;

    // Temporary vectors
    std::vector<int> jrow, jcol;
    
    // Loop over the variables, ndir variables at a time
    for(int s=0; s<nsweep; ++s){
      
      // Print progress
      if(verbose()){
        int progress_new = (s*100)/nsweep;
        // Print when entering a new decade
        if(progress_new / 10 > progress / 10){
          progress = progress_new;
          std::cout << progress << " %"  << std::endl;
        }
      }
      
      // Nonzero offset
      int offset = s*bvec_size;

      // Number of local seed directions
      int ndir_local = std::min(bvec_size,nz_seed-offset);

      for(int i=0; i<ndir_local; ++i){
        seed_v[offset+i] |= bvec_t(1)<<i;
      }
      
      // Propagate the dependencies
      spEvaluate(use_fwd);
      
      // Loop over the nonzeros of the output
      for(int el=0; el<nz_sens; ++el){

        // Get the sparsity sensitivity
        bvec_t spsens = sens_v[el];

        // Clear the sensitivities for the next sweep
        if(!use_fwd){
          sens_v[el] = 0;
        }
  
        // If there is a dependency in any of the directions
        if(0!=spsens){
          
          // Loop over seed directions
          for(int i=0; i<ndir_local; ++i){
            
            // If dependents on the variable
            if((bvec_t(1) << i) & spsens){
              // Add to pattern
              jrow.push_back(el);
              jcol.push_back(i+offset);
            }
          }
        }
      }
      
      // Remove the seeds
      for(int i=0; i<bvec_size && offset+i<nz_seed; ++i){
        seed_v[offset+i] = 0;
      }
    }
    
    // Set inputs and outputs to zero
    for(int ind=0; ind<getNumInputs(); ++ind) input(ind).setZero();
    for(int ind=0; ind<getNumOutputs(); ++ind) output(ind).setZero();

    // Construct sparsity pattern
    CRSSparsity ret = sp_triplet(nz_out, nz_in,use_fwd ? jrow : jcol, use_fwd ? jcol : jrow);
    
    // Return sparsity pattern
    if(verbose()){
      std::cout << "Formed Jacobian sparsity pattern (dimension " << ret.shape() << ", " << 100*double(ret.size())/ret.numel() << " \% nonzeros)." << endl;
      std::cout << "FXInternal::getJacSparsity end " << std::endl;
    }
    return ret;
}

CRSSparsity FXInternal::getJacSparsityHierarchicalSymm(int iind, int oind){
    casadi_assert(spCanEvaluate(true));

    // Number of nonzero inputs
    int nz = input(iind).size();
    
    // Clear the forward seeds/adjoint sensitivities
    for(int ind=0; ind<getNumInputs(); ++ind){
      vector<double> &v = inputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }

    // Clear the adjoint seeds/forward sensitivities
    for(int ind=0; ind<getNumOutputs(); ++ind){
      vector<double> &v = outputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }

    // Sparsity triplet accumulator
    std::vector<int> jrow, jcol;
    
    // Rows/cols of the coarse blocks
    std::vector<int> coarse(2,0); coarse[1] = nz;
    
    // Rows/cols of the fine blocks
    std::vector<int> fine;
    
    // In each iteration, subdivide each coarse block in this many fine blocks
    int subdivision = bvec_size;

    CRSSparsity r = sp_dense(1,1);
    
    // The size of a block
    int granularity = nz;
    
    int nsweeps = 0;
    
    bool hasrun = false;
    
    while (!hasrun || coarse.size()!=nz+1) {
      casadi_log("Block size: " << granularity);
    
      // Clear the sparsity triplet acccumulator
      jrow.clear();
      jcol.clear();
      
      // Clear the fine block structure
      fine.clear();
      
      CRSSparsity D = r.starColoring();

      casadi_log("Star coloring: " << D.size1() << " <-> " << D.size2());
      
      // Reset the virtual machine
      spInit(true);
      
      // Get seeds and sensitivities
      bvec_t* input_v = get_bvec_t(inputNoCheck(iind).data());
      bvec_t* output_v = get_bvec_t(outputNoCheck(oind).data());
      bvec_t* seed_v = input_v;
      bvec_t* sens_v = output_v;
      
      // Clear the seeds
      for(int i=0; i<nz; ++i) seed_v[i]=0;
      
      // Subdivide the coarse block
      for (int k=0;k<coarse.size()-1;++k) {
        int diff = coarse[k+1]-coarse[k];
        int new_diff = diff/subdivision;
        if(diff%subdivision>0) new_diff++;
        std::vector<int> temp = range(coarse[k],coarse[k+1],new_diff);
        fine.insert(fine.end(),temp.begin(),temp.end());
      }
      if (fine.back()!=coarse.back()) fine.push_back(coarse.back());

      granularity = fine[1] - fine[0];
      
      // The index into the bvec bit vector
      int bvec_i = 0;
      
      // Create lookup tables for the fine blocks
      std::vector<int> fine_lookup = lookupvector(fine,nz+1);
      
      // Triplet data used as a lookup table
      std::vector<int> lookup_row;
      std::vector<int> lookup_col;
      std::vector<int> lookup_value;
      
      // Loop over all coarse seed directions from the coloring
      for(int csd=0; csd<D.size1(); ++csd) {
         // The maximum number of fine blocks contained in one coarse block
         int n_fine_blocks_max = fine_lookup[coarse[1]]-fine_lookup[coarse[0]];
         
         int fci_offset = 0;
         int fci_cap = bvec_size-bvec_i;
         
         // Flag to indicate if all fine blocks have been handled
         bool f_finished = false;
         
         // Loop while not finished
         while(!f_finished) {
    
           // Loop over all coarse columns that are found in the coloring for this coarse seed direction
           for(int k=D.rowind()[csd]; k<D.rowind()[csd+1]; ++k){
             int cci = D.col()[k];
             
             // The first and last columns of the fine block
             int fci_start = fine_lookup[coarse[cci]];
             int fci_end   = fine_lookup[coarse[cci+1]];
             
             // Local counter that modifies index into bvec
             int bvec_i_mod = 0;
             
             int value = -bvec_i + fci_offset + fci_start;
             
             //casadi_assert(value>=0);
             
             // Loop over the columns of the fine block
             for (int fci = fci_offset;fci<min(fci_end-fci_start,fci_cap);++fci) {
             
               // Loop over the coarse block rows that appear in the coloring for the current coarse seed direction
               for (int cri=r.rowind()[cci];cri<r.rowind()[cci+1];++cri) {
                 lookup_row.push_back(r.col()[cri]);
                 lookup_col.push_back(bvec_i+bvec_i_mod);
                 lookup_value.push_back(value);
               }

               // Toggle on seeds
               bvec_toggle(seed_v,fine[fci+fci_start],fine[fci+fci_start+1],bvec_i+bvec_i_mod);
               bvec_i_mod++;
             }
           }
           
           // Bump bvec_i for next major coarse direction
           bvec_i+= min(n_fine_blocks_max,fci_cap);
           
           // Check if bvec buffer is full
           if (bvec_i==bvec_size || csd==D.size1()-1) {
              // Calculate sparsity for bvec_size directions at once
              
              // Statistics
              nsweeps+=1;
              
              // Construct lookup table
              IMatrix lookup = IMatrix::sparse(lookup_row,lookup_col,lookup_value,coarse.size(),bvec_size);

              std::reverse(lookup_row.begin(),lookup_row.end());
              std::reverse(lookup_col.begin(),lookup_col.end());
              std::reverse(lookup_value.begin(),lookup_value.end());
              IMatrix duplicates = IMatrix::sparse(lookup_row,lookup_col,lookup_value,coarse.size(),bvec_size) - lookup;
              makeSparse(duplicates);
              lookup(duplicates.sparsity()) = -bvec_size;
              
              // Propagate the dependencies
              spEvaluate(true);
              
              // Temporary bit work vector
              bvec_t spsens;
              
              // Loop over the rows of coarse blocks
              for (int cri=0;cri<coarse.size()-1;++cri) {

                // Loop over the rows of fine blocks within the current coarse block
                for (int fri=fine_lookup[coarse[cri]];fri<fine_lookup[coarse[cri+1]];++fri) {
                  // Lump individual sensitivities together into fine block
                  bvec_or(sens_v,spsens,fine[fri],fine[fri+1]);
  
                  // Loop over all bvec_bits
                  for (int bvec_i=0;bvec_i<bvec_size;++bvec_i) {
                    if (spsens & (bvec_t(1) << bvec_i)) {
                      // if dependency is found, add it to the new sparsity pattern
                      int lk = lookup.elem(cri,bvec_i);
                      if (lk>-bvec_size) {
                        jcol.push_back(bvec_i+lk);
                        jrow.push_back(fri);
                        jcol.push_back(fri);
                        jrow.push_back(bvec_i+lk);
                      }
                    }
                  }
                }
              }
              
              // Clean seed vector, ready for next bvec sweep
              for(int i=0; i<nz; ++i) seed_v[i]=0;
              
              // Clean lookup table
              lookup_row.clear();
              lookup_col.clear();
              lookup_value.clear();
           }
           
           if (n_fine_blocks_max>fci_cap) {
             fci_offset += min(n_fine_blocks_max,fci_cap);
             bvec_i = 0;
             fci_cap = bvec_size;
           } else {
             f_finished = true;
           }
         
         }
         
      }

      // Construct fine sparsity pattern
      r = sp_triplet(fine.size()-1, fine.size()-1, jrow, jcol);
      coarse = fine;
      hasrun = true;
    }
    
    casadi_log("Number of sweeps: " << nsweeps );
    
    return r;
}

CRSSparsity FXInternal::getJacSparsityHierarchical(int iind, int oind){
 // Number of nonzero inputs
    int nz_in = input(iind).size();
    
    // Number of nonzero outputs
    int nz_out = output(oind).size();
    
    // Clear the forward seeds/adjoint sensitivities
    for(int ind=0; ind<getNumInputs(); ++ind){
      vector<double> &v = inputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }

    // Clear the adjoint seeds/forward sensitivities
    for(int ind=0; ind<getNumOutputs(); ++ind){
      vector<double> &v = outputNoCheck(ind).data();
      if(!v.empty()) fill_n(get_bvec_t(v),v.size(),bvec_t(0));
    }

    // Sparsity triplet accumulator
    std::vector<int> jrow, jcol;
    
    // Rows of the coarse blocks
    std::vector<int> coarse_row(2,0); coarse_row[1] = nz_out;
    // Cols of the coarse blocks
    std::vector<int> coarse_col(2,0); coarse_col[1] = nz_in;
    
    // Rows of the fine blocks
    std::vector<int> fine_row;
    
    // Cols of the fine blocks
    std::vector<int> fine_col;
    
    // In each iteration, subdivide each coarse block in this many fine blocks
    int subdivision = bvec_size;

    CRSSparsity r = sp_dense(1,1);
    
    // The size of a block
    int granularity_col = nz_in;
    int granularity_row = nz_out;
    
    bool use_fwd = true;
    
    int nsweeps = 0;
    
    bool hasrun = false;
    
    while (!hasrun || coarse_row.size()!=nz_out+1 || coarse_col.size()!=nz_in+1) {
      casadi_log("Block size: " << granularity_row << " x " << granularity_col);
    
      // Clear the sparsity triplet acccumulator
      jrow.clear();
      jcol.clear();
      
      // Clear the fine block structure
      fine_col.clear();
      fine_row.clear();
    
      // r transpose will be needed in the algorithm
      CRSSparsity rT = r.transpose();
      
      /**       Decide which ad_mode to take           */
      CRSSparsity D1 = rT.unidirectionalColoring(r);
      CRSSparsity D2 = r.unidirectionalColoring(rT);
      
      // Adjoint mode penalty factor (adjoint mode is usually more expensive to calculate)
      int adj_penalty = 2;
      
      int fwd_cost = use_fwd ? granularity_col: granularity_row;
      int adj_cost = use_fwd ? granularity_row: granularity_col;
      
      // Use whatever required less colors if we tried both (with preference to forward mode)
      if((D1.size1()*fwd_cost <= adj_penalty*D2.size1()*adj_cost)){
        use_fwd = true;
        casadi_log("Forward mode chosen: " << D1.size1()*fwd_cost << " <-> " << adj_penalty*D2.size1()*adj_cost);
      } else {
        use_fwd = false;
        casadi_log("Adjoint mode chosen: " << D1.size1()*fwd_cost << " <-> " << adj_penalty*D2.size1()*adj_cost);
      }
      
      use_fwd = spCanEvaluate(true) && use_fwd;
          
      // Override default behavior?
      if(getOption("ad_mode") == "forward"){
        use_fwd = true;
      } else if(getOption("ad_mode") == "reverse"){
        use_fwd = false;
      }
      
      // Reset the virtual machine
      spInit(use_fwd);
      
      // Get seeds and sensitivities
      bvec_t* input_v = get_bvec_t(inputNoCheck(iind).data());
      bvec_t* output_v = get_bvec_t(outputNoCheck(oind).data());
      bvec_t* seed_v = use_fwd ? input_v : output_v;
      bvec_t* sens_v = use_fwd ? output_v : input_v;
      
      // The number of zeros in the seed and sensitivity directions
      int nz_seed = use_fwd ? nz_in  : nz_out;
      int nz_sens = use_fwd ? nz_out : nz_in;
      
      // Clear the seeds
      for(int i=0; i<nz_seed; ++i) seed_v[i]=0;
      
      // Choose the active jacobian coloring scheme
      CRSSparsity D = use_fwd ? D1 : D2;
      
      // Adjoint mode amounts to swapping
      if (!use_fwd) {
        std::swap(coarse_row,coarse_col);
        std::swap(granularity_row,granularity_col);
        std::swap(r,rT);
      }
      
      // Subdivide the coarse block rows
      for (int k=0;k<coarse_row.size()-1;++k) {
        int diff = coarse_row[k+1]-coarse_row[k];
        int new_diff = diff/subdivision;
        if(diff%subdivision>0) new_diff++;
        std::vector<int> temp = range(coarse_row[k],coarse_row[k+1],new_diff);
        fine_row.insert(fine_row.end(),temp.begin(),temp.end());
      }
      // Subdivide the coarse block columns
      for (int k=0;k<coarse_col.size()-1;++k) {
        int diff = coarse_col[k+1]-coarse_col[k];
        int new_diff = diff/subdivision;
        if(diff%subdivision>0) new_diff++;
        std::vector<int> temp = range(coarse_col[k],coarse_col[k+1],new_diff);
        fine_col.insert(fine_col.end(),temp.begin(),temp.end());
      }
      if (fine_col.back()!=coarse_col.back()) fine_col.push_back(coarse_col.back());
      if (fine_row.back()!=coarse_row.back()) fine_row.push_back(coarse_row.back());

      granularity_row = fine_row[1] - fine_row[0];
      granularity_col = fine_col[1] - fine_col[0];
      
      // The index into the bvec bit vector
      int bvec_i = 0;
      
      // Create lookup tables for the fine blocks
      std::vector<int> fine_row_lookup = lookupvector(fine_row,nz_sens+1);
      std::vector<int> fine_col_lookup = lookupvector(fine_col,nz_seed+1);
      
      // Triplet data used as a lookup table
      std::vector<int> lookup_row;
      std::vector<int> lookup_col;
      std::vector<int> lookup_value;
      
      // Loop over all coarse seed directions from the coloring
      for(int csd=0; csd<D.size1(); ++csd) {
      
         // The maximum number of fine blocks contained in one coarse block
         int n_fine_blocks_max = fine_col_lookup[coarse_col[1]]-fine_col_lookup[coarse_col[0]];
         
         int fci_offset = 0;
         int fci_cap = bvec_size-bvec_i;
         
         // Flag to indicate if all fine blocks have been handled
         bool f_finished = false;
         
         // Loop while not finished
         while(!f_finished) {
    
           // Loop over all coarse columns that are found in the coloring for this coarse seed direction
           for(int k=D.rowind()[csd]; k<D.rowind()[csd+1]; ++k){
             int cci = D.col()[k];

             // The first and last columns of the fine block
             int fci_start = fine_col_lookup[coarse_col[cci]];
             int fci_end   = fine_col_lookup[coarse_col[cci+1]];
             
             // Local counter that modifies index into bvec
             int bvec_i_mod = 0;
             
             int value = -bvec_i + fci_offset + fci_start;
             
             // Loop over the columns of the fine block
             for (int fci = fci_offset;fci<min(fci_end-fci_start,fci_cap);++fci) {
             
               // Loop over the coarse block rows that appear in the coloring for the current coarse seed direction
               for (int cri=rT.rowind()[cci];cri<rT.rowind()[cci+1];++cri) {
                 lookup_row.push_back(rT.col()[cri]);
                 lookup_col.push_back(bvec_i+bvec_i_mod);
                 lookup_value.push_back(value);
               }

               // Toggle on seeds
               bvec_toggle(seed_v,fine_col[fci+fci_start],fine_col[fci+fci_start+1],bvec_i+bvec_i_mod);
               bvec_i_mod++;
             }
           }
           
           // Bump bvec_i for next major coarse direction
           bvec_i+= min(n_fine_blocks_max,fci_cap);
           
           // Check if bvec buffer is full
           if (bvec_i==bvec_size || csd==D.size1()-1) {
              // Calculate sparsity for bvec_size directions at once
              
              // Statistics
              nsweeps+=1; 
              
              // Construct lookup table
              IMatrix lookup = IMatrix::sparse(lookup_row,lookup_col,lookup_value,coarse_row.size(),bvec_size);

              // Propagate the dependencies
              spEvaluate(use_fwd);
              
              // Temporary bit work vector
              bvec_t spsens;
              
              // Loop over the rows of coarse blocks
              for (int cri=0;cri<coarse_row.size()-1;++cri) {

                // Loop over the rows of fine blocks within the current coarse block
                for (int fri=fine_row_lookup[coarse_row[cri]];fri<fine_row_lookup[coarse_row[cri+1]];++fri) {
                  // Lump individual sensitivities together into fine block
                  bvec_or(sens_v,spsens,fine_row[fri],fine_row[fri+1]);
  
                  // Loop over all bvec_bits
                  for (int bvec_i=0;bvec_i<bvec_size;++bvec_i) {
                    if (spsens & (bvec_t(1) << bvec_i)) {
                      // if dependency is found, add it to the new sparsity pattern
                      jcol.push_back(bvec_i+lookup.elem(cri,bvec_i));
                      jrow.push_back(fri);
                    }
                  }
                }
              }
              
              // Clean seed vector, ready for next bvec sweep
              for(int i=0; i<nz_seed; ++i) seed_v[i]=0;
              
              // Clean lookup table
              lookup_row.clear();
              lookup_col.clear();
              lookup_value.clear();
           }
           
           if (n_fine_blocks_max>fci_cap) {
             fci_offset += min(n_fine_blocks_max,fci_cap);
             bvec_i = 0;
             fci_cap = bvec_size;
           } else {
             f_finished = true;
           }
         
         }
         
      }

      // Swap results if adjoint mode was used
      if (use_fwd) {
       // Construct fine sparsity pattern
       r = sp_triplet(fine_row.size()-1, fine_col.size()-1, jrow, jcol);
       coarse_row = fine_row;
       coarse_col = fine_col;
      } else {
       // Construct fine sparsity pattern
       r = sp_triplet( fine_col.size()-1,fine_row.size()-1, jcol, jrow);
       coarse_row = fine_col;
       coarse_col = fine_row;
      }
      hasrun = true;
    }
    casadi_log("Number of sweeps: " << nsweeps );
    
    return r;
}

CRSSparsity FXInternal::getJacSparsity(int iind, int oind, bool symmetric){
  // Check if we are able to propagate dependencies through the function
  if(spCanEvaluate(true) || spCanEvaluate(false)){
    
    if (input(iind).size()>1 && output(oind).size()>1) {
      if (symmetric) {
        return getJacSparsityHierarchicalSymm(iind, oind);
      } else {
        return getJacSparsityHierarchical(iind, oind);
      }
    } else {
      return getJacSparsityPlain(iind, oind);
    }


  } else {
    // Dense sparsity by default
    return CRSSparsity(output(oind).size(),input(iind).size(),true);
  }
}

void FXInternal::setJacSparsity(const CRSSparsity& sp, int iind, int oind, bool compact){
  if(compact){
    jac_sparsity_compact_[iind][oind] = sp;
  } else {
    jac_sparsity_[iind][oind] = sp;
  }
}

CRSSparsity& FXInternal::jacSparsity(int iind, int oind, bool compact, bool symmetric){
  casadi_assert_message(isInit(),"Function not initialized.");

  // Get a reference to the block
  CRSSparsity& jsp = compact ? jac_sparsity_compact_[iind][oind] : jac_sparsity_[iind][oind];

  // Generate, if null
  if(jsp.isNull()){
    if(compact){
      if(spgen_==0){
        // Use internal routine to determine sparsity
        jsp = getJacSparsity(iind,oind,symmetric);
      } else {
        // Create a temporary FX instance
        FX tmp;
        tmp.assignNode(this);

        // Use user-provided routine to determine sparsity
        jsp = spgen_(tmp,iind,oind,user_data_);
      }
    } else {
      
      // Get the compact sparsity pattern
      CRSSparsity sp = jacSparsity(iind,oind,true,symmetric);

      // Enlarge if sparse output 
      if(output(oind).numel()!=sp.size1()){
        casadi_assert(sp.size1()==output(oind).size());
        
        // New row for each old row 
        vector<int> row_map = output(oind).sparsity().getElements();
    
        // Insert rows 
        sp.enlargeRows(output(oind).numel(),row_map);
      }
  
      // Enlarge if sparse input 
      if(input(iind).numel()!=sp.size2()){
        casadi_assert(sp.size2()==input(iind).size());
        
        // New column for each old column
        vector<int> col_map = input(iind).sparsity().getElements();
        
        // Insert columns
        sp.enlargeColumns(input(iind).numel(),col_map);
      }
      
      // Save
      jsp = sp;
    }
  }
  
  // If still null, not dependent
  if(jsp.isNull()){
    jsp = CRSSparsity(output(oind).size(),input(iind).size());
  }
  
  // Return a reference to the block
  return jsp;
}

void FXInternal::getPartition(int iind, int oind, CRSSparsity& D1, CRSSparsity& D2, bool compact, bool symmetric){
  log("FXInternal::getPartition begin");
  
  // Sparsity pattern with transpose
  CRSSparsity &A = jacSparsity(iind,oind,compact,symmetric);
  vector<int> mapping;
  CRSSparsity AT = symmetric ? A : A.transpose(mapping);
  mapping.clear();
  
  // Which AD mode?
  bool test_ad_fwd=true, test_ad_adj=true;
  if(getOption("ad_mode") == "forward"){
    test_ad_adj = false;
  } else if(getOption("ad_mode") == "reverse"){
    test_ad_fwd = false;
  } else if(getOption("ad_mode") != "automatic"){
    casadi_error("FXInternal::jac: Unknown ad_mode \"" << getOption("ad_mode") << "\". Possible values are \"forward\", \"reverse\" and \"automatic\".");
  }
  
  // Get seed matrices by graph coloring
  if(symmetric){
  
    // Star coloring if symmetric
    log("FXInternal::getPartition starColoring");
    D1 = A.starColoring();
    if(verbose()){
      cout << "Star coloring completed: " << D1.size1() << " directional derivatives needed (" << A.size2() << " without coloring)." << endl;
    }
    
  } else {
    
    // Test unidirectional coloring using forward mode
    if(test_ad_fwd){
      log("FXInternal::getPartition unidirectional coloring (forward mode)");
      D1 = AT.unidirectionalColoring(A);
      if(verbose()){
        cout << "Forward mode coloring completed: " << D1.size1() << " directional derivatives needed (" << A.size2() << " without coloring)." << endl;
      }
    }
      
    // Test unidirectional coloring using reverse mode
    if(test_ad_adj){
      log("FXInternal::getPartition unidirectional coloring (adjoint mode)");
      D2 = A.unidirectionalColoring(AT);
      if(verbose()){
        cout << "Adjoint mode coloring completed: " << D2.size1() << " directional derivatives needed (" << A.size1() << " without coloring)." << endl;
      }
    }
    
    // Adjoint mode penalty factor (adjoint mode is usually more expensive to calculate)
    int adj_penalty = 2;

    // Use whatever required less colors if we tried both (with preference to forward mode)
    if(test_ad_fwd && test_ad_adj){
      if((D1.size1() <= adj_penalty*D2.size1())){
        D2=CRSSparsity();
        log("Forward mode chosen");
      } else {
        D1=CRSSparsity();
        log("Adjoint mode chosen");
      }
    }
    log("FXInternal::getPartition end");
  }
}

void FXInternal::evaluateCompressed(int nfdir, int nadir){
  // Counter for compressed forward directions
  int nfdir_compressed=0;

  // Check if any forward directions are all zero
  for(int dir=0; dir<nfdir; ++dir){
    
    // Can we compress this direction?
    compressed_fwd_[dir] = true;

    // Look out for nonzeros
    for(int ind=0; compressed_fwd_[dir] && ind<getNumInputs(); ++ind){
      const vector<double>& v = fwdSeedNoCheck(ind,dir).data();
      for(vector<double>::const_iterator it=v.begin(); compressed_fwd_[dir] && it!=v.end(); ++it){
	if(*it!=0) compressed_fwd_[dir]=false;
      }
    }

    // Skip if we can indeed compress
    if(compressed_fwd_[dir]) continue;
    
    // Move the direction to a previous direction if different
    if(dir!=nfdir_compressed){
      for(int ind=0; ind<getNumInputs(); ++ind){
	const vector<double>& v_old = fwdSeedNoCheck(ind,dir).data();
	vector<double>& v_new = fwdSeedNoCheck(ind,nfdir_compressed).data();
	copy(v_old.begin(),v_old.end(),v_new.begin());
      }
    }
    
    // Increase direction counter
    nfdir_compressed++;
  }

  // Counter for compressed adjoint directions
  int nadir_compressed=0;

  // Check if any adjoint directions are all zero
  for(int dir=0; dir<nadir; ++dir){
    
    // Can we compress this direction?
    compressed_adj_[dir] = true;

    // Look out for nonzeros
    for(int ind=0; compressed_adj_[dir] && ind<getNumOutputs(); ++ind){
      const vector<double>& v = adjSeedNoCheck(ind,dir).data();
      for(vector<double>::const_iterator it=v.begin(); compressed_adj_[dir] && it!=v.end(); ++it){
	if(*it!=0) compressed_adj_[dir]=false;
      }
    }

    // Skip if we can indeed compress
    if(compressed_adj_[dir]) continue;
    
    // Move the direction to a previous direction if different
    if(dir!=nadir_compressed){
      for(int ind=0; ind<getNumOutputs(); ++ind){
	const vector<double>& v_old = adjSeedNoCheck(ind,dir).data();
	vector<double>& v_new = adjSeedNoCheck(ind,nadir_compressed).data();
	copy(v_old.begin(),v_old.end(),v_new.begin());
      }
    }
    
    // Increase direction counter
    nadir_compressed++;
  }
  
  // Evaluate compressed
  evaluate(nfdir_compressed,nadir_compressed);

  // Decompress forward directions in reverse order
  for(int dir=nfdir-1; dir>=0; --dir){
    if(compressed_fwd_[dir]){
      for(int ind=0; ind<getNumOutputs(); ++ind){
	fwdSensNoCheck(ind,dir).setZero();
      }
    } else if(--nfdir_compressed != dir){
      for(int ind=0; ind<getNumOutputs(); ++ind){
	const vector<double>& v_old = fwdSensNoCheck(ind,nfdir_compressed).data();
	vector<double>& v_new = fwdSensNoCheck(ind,dir).data();
	copy(v_old.begin(),v_old.end(),v_new.begin());
      }
    }
  }

  // Decompress adjoint directions in reverse order
  for(int dir=nadir-1; dir>=0; --dir){
    if(compressed_adj_[dir]){
      for(int ind=0; ind<getNumInputs(); ++ind){
	adjSensNoCheck(ind,dir).setZero();
      }
    } else if(--nadir_compressed != dir){
      for(int ind=0; ind<getNumInputs(); ++ind){
	const vector<double>& v_old = adjSensNoCheck(ind,nadir_compressed).data();
	vector<double>& v_new = adjSensNoCheck(ind,dir).data();
	copy(v_old.begin(),v_old.end(),v_new.begin());
      }
    }
  }
}

void FXInternal::evalSX(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
      const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
      const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
      bool output_given){
  casadi_error("FXInternal::evalSX not defined for class " << typeid(*this).name());
}

void FXInternal::evalMX(const std::vector<MX>& arg, std::vector<MX>& res, 
      const std::vector<std::vector<MX> >& fseed, std::vector<std::vector<MX> >& fsens, 
      const std::vector<std::vector<MX> >& aseed, std::vector<std::vector<MX> >& asens,
      bool output_given){
  casadi_error("FXInternal::evalMX not defined for class " << typeid(*this).name());
}

void FXInternal::spEvaluate(bool fwd){
  // By default, everything is assumed to depend on everything
  
  // Variable which depends on all everything
  bvec_t all_depend(0);
  if(fwd){
    // Get dependency on all inputs
    for(int iind=0; iind<getNumInputs(); ++iind){
      const DMatrix& m = inputNoCheck(iind);
      const bvec_t* v = reinterpret_cast<const bvec_t*>(m.ptr());
      for(int i=0; i<m.size(); ++i){
        all_depend |= v[i];
      }
    }
    
    // Propagate to all outputs
    for(int oind=0; oind<getNumOutputs(); ++oind){
      DMatrix& m = outputNoCheck(oind);
      bvec_t* v = reinterpret_cast<bvec_t*>(m.ptr());
      for(int i=0; i<m.size(); ++i){
        v[i] = all_depend;
      }
    }
    
  } else {
    
    // Get dependency on all outputs
    for(int oind=0; oind<getNumOutputs(); ++oind){
      const DMatrix& m = outputNoCheck(oind);
      const bvec_t* v = reinterpret_cast<const bvec_t*>(m.ptr());
      for(int i=0; i<m.size(); ++i){
        all_depend |= v[i];
      }
    }
    
    // Propagate to all inputs
    for(int iind=0; iind<getNumInputs(); ++iind){
      DMatrix& m = inputNoCheck(iind);
      bvec_t* v = reinterpret_cast<bvec_t*>(m.ptr());
      for(int i=0; i<m.size(); ++i){
        v[i] |= all_depend;
      }
    }
  }
}

FX FXInternal::jacobian(int iind, int oind, bool compact, bool symmetric){
  // Return value
  FX ret;
  
  // Generate Jacobian
  if(jacgen_!=0){
    // Use user-provided routine to calculate Jacobian
    FX fcn = shared_from_this<FX>();
    ret = jacgen_(fcn,iind,oind,user_data_);
  } else if(bool(getOption("numeric_jacobian"))){
    ret = getNumericJacobian(iind,oind,compact,symmetric);
  } else {
    // Use internal routine to calculate Jacobian
    ret = getJacobian(iind,oind,compact, symmetric);
  }
  
  // Give it a suitable name
  stringstream ss;
  ss << "jacobian_" << getOption("name") << "_" << iind << "_" << oind;
  ret.setOption("name",ss.str());
  ret.setOption("verbose",getOption("verbose"));
  return ret;
}

FX FXInternal::getJacobian(int iind, int oind, bool compact, bool symmetric){
  return getNumericJacobian(iind,oind,compact,symmetric);
}

FX FXInternal::derivative(int nfwd, int nadj){
  // Quick return if 0x0
  if(nfwd==0 && nadj==0) return shared_from_this<FX>();

  // Check if there are enough forward directions allocated
  if(nfwd>=derivative_fcn_.size()){
    derivative_fcn_.resize(nfwd+1);
  }

  // Check if there are enough adjoint directions allocated
  if(nadj>=derivative_fcn_[nfwd].size()){
    derivative_fcn_[nfwd].resize(nadj+1);
  }

  // Return value
  FX& ret = derivative_fcn_[nfwd][nadj];

  // Generate if not already cached
  if(ret.isNull()){

    // Get the number of scalar inputs and outputs
    int num_in_scalar = getNumScalarInputs();
    int num_out_scalar = getNumScalarOutputs();
  
    // Adjoint mode penalty factor (adjoint mode is usually more expensive to calculate)
    int adj_penalty = 2;
    
    // Crude estimate of the cost of calculating the full Jacobian
    int full_jac_cost = std::min(num_in_scalar, adj_penalty*num_out_scalar);
    
    // Crude estimate of the cost of calculating the directional derivatives
    int der_dir_cost = nfwd + adj_penalty*nadj;
    
    // Check if it is cheaper to calculate the full Jacobian and then multiply
    if(2*full_jac_cost < der_dir_cost){
      // Generate the Jacobian and then multiply to get the derivative
      //ret = getDerivativeViaJac(nfwd,nadj); // NOTE: Uncomment this line (and remove the next line) to enable this feature
      ret = getDerivative(nfwd,nadj);    
    } else {
      // Generate a new function
      ret = getDerivative(nfwd,nadj);    
    }
    
    // Give it a suitable name
    stringstream ss;
    ss << "derivative_" << getOption("name") << "_" << nfwd << "_" << nadj;
    ret.setOption("name",ss.str());
    
    // Initialize it
    ret.init();
  }

  // Return cached or generated function
  return ret;
}

FX FXInternal::getDerivative(int nfwd, int nadj){
  casadi_error("FXInternal::getDerivative not defined for class " << typeid(*this).name());
}

FX FXInternal::getDerivativeViaJac(int nfwd, int nadj){
  // Wrap in an MXFunction
  vector<MX> arg = symbolicInput();
  vector<MX> res = shared_from_this<FX>().call(arg);
  FX f = MXFunction(arg,res);
  f.init();
  return f->getDerivativeViaJac(nfwd,nadj);
}

int FXInternal::getNumScalarInputs() const{
  int ret=0;
  for(int iind=0; iind<getNumInputs(); ++iind){
    ret += input(iind).size();
  }
  return ret;
}

int FXInternal::getNumScalarOutputs() const{
  int ret=0;
  for(int oind=0; oind<getNumOutputs(); ++oind){
    ret += output(oind).size();
  }
  return ret;
}

int FXInternal::inputSchemeEntry(const std::string &name) const {
  return schemeEntry(inputScheme,name);
}

int FXInternal::outputSchemeEntry(const std::string &name) const {
  return schemeEntry(outputScheme,name);
}

int FXInternal::schemeEntry(InputOutputScheme scheme, const std::string &name) const {
  if (scheme==SCHEME_unknown) casadi_error("Unable to look up '" <<  name<< "' in input scheme, as the input scheme of this function is unknown. You can only index with integers.");
  if (name=="") casadi_error("FXInternal::inputSchemeEntry: you supplied an empty string as the name of a entry in " << getSchemeName(scheme) << ". Available names are: " << getSchemeEntryNames(scheme) << ".");
  int n = getSchemeEntryEnum(scheme,name);
  if (n==-1) casadi_error("FXInternal::inputSchemeEntry: could not find entry '" << name << "' in " << getSchemeName(scheme) << ". Available names are: " << getSchemeEntryNames(scheme) << ".");
  return n;
}

void FXInternal::call(const MXVector& arg, MXVector& res,  const MXVectorVector& fseed, MXVectorVector& fsens, 
                      const MXVectorVector& aseed, MXVectorVector& asens, 
                      bool output_given, bool always_inline, bool never_inline){
  casadi_assert_message(!(always_inline && never_inline), "Inconsistent options");
  
  // Lo logic for inlining yet
  bool inline_function = always_inline;
  
  if(inline_function){
    // Evaluate the function symbolically
    evalMX(arg,res,fseed,fsens,aseed,asens,output_given);
    
  } else {
    // Create a call-node
    assertInit();
    
    // Argument checking
    casadi_assert_message(arg.size()<=getNumInputs(), "FX::call: number of passed-in dependencies (" << arg.size() << ") should not exceed the number of inputs of the function (" << getNumInputs() << ").");

    // Assumes initialised
    for(int i=0; i<arg.size(); ++i){
      if(arg[i].isNull() || arg[i].empty() || input(i).isNull() || input(i).empty()) continue;
      casadi_assert_message(arg[i].size1()==input(i).size1() && arg[i].size2()==input(i).size2(),
                            "Evaluation::shapes of passed-in dependencies should match shapes of inputs of function." << 
                            std::endl << "Input argument " << i << " has shape (" << input(i).size1() << 
                            "," << input(i).size2() << ") while a shape (" << arg[i].size1() << "," << arg[i].size2() << 
                            ") was supplied.");
    }
    EvaluationMX::create(shared_from_this<FX>(),arg,res,fseed,fsens,aseed,asens,output_given);
  }
}

void FXInternal::call(const std::vector<SXMatrix>& arg, std::vector<SXMatrix>& res, 
                      const std::vector<std::vector<SXMatrix> >& fseed, std::vector<std::vector<SXMatrix> >& fsens, 
                      const std::vector<std::vector<SXMatrix> >& aseed, std::vector<std::vector<SXMatrix> >& asens,
                      bool output_given, bool always_inline, bool never_inline){
  casadi_assert_message(!(always_inline && never_inline), "Inconsistent options");
  casadi_assert_message(!never_inline, "SX expressions do not support call-nodes");
  evalSX(arg,res,fseed,fsens,aseed,asens,output_given);
}

FX FXInternal::getNumericJacobian(int iind, int oind, bool compact, bool symmetric){
  vector<MX> arg = symbolicInput();
  vector<MX> res = shared_from_this<FX>().call(arg);
  FX f = MXFunction(arg,res);
  f.setOption("numeric_jacobian", false); // BUG ?
  f.init();
  return f->getNumericJacobian(iind,oind,compact,symmetric);
}

  FX FXInternal::fullJacobian(){
    if(full_jacobian_.alive()){
      // Return cached Jacobian
      return shared_cast<FX>(full_jacobian_.shared());
    } else {
      // Generate a new Jacobian
      FX ret;
      if(getNumInputs()==1 && getNumOutputs()==1){
	ret = jacobian(0,0,true,false);
      } else {
	getFullJacobian();
      }

      // Return and cache it for reuse
      full_jacobian_ = ret;
      return ret;
    }
  }

  FX FXInternal::getFullJacobian(){
    // Count the total number of inputs and outputs
    int num_in_scalar = getNumScalarInputs();
    int num_out_scalar = getNumScalarOutputs();

    // Generate a function from all input nonzeros to all output nonzeros
    MX arg = msym("arg",num_in_scalar);
    
    // Assemble vector inputs
    int nz_offset = 0; // Current nonzero offset
    vector<MX> argv(getNumInputs());
    for(int ind=0; ind<argv.size(); ++ind){
      const CRSSparsity& sp = input(ind).sparsity();
      argv[ind] = reshape(arg[Slice(nz_offset,nz_offset+sp.size())],sp);
      nz_offset += sp.size();
    }
    casadi_assert(nz_offset == num_in_scalar);

    // Call function to get vector outputs
    vector<MX> resv = shared_from_this<FX>().call(argv);

    // Get the nonzeros of each output
    for(int ind=0; ind<resv.size(); ++ind){
      if(resv[ind].size2()!=1 || !resv[ind].dense()){
	resv[ind] = resv[ind][Slice()];
      }
    }

    // Concatenate to get all output nonzeros
    MX res = vertcat(resv);
    casadi_assert(res.size() == num_out_scalar);

    // Form function of all inputs nonzeros to all output nonzeros and return Jacobian of this
    FX f = MXFunction(arg,res);
    f.init();
    return f.jacobian(0,0,false,false);
  }


void FXInternal::generateCode(const string& src_name){
  casadi_error("FXInternal::generateCode: generateCode not defined for class " << typeid(*this).name());
}

void FXInternal::printVector(std::ostream &cfile, const std::string& name, const vector<int>& v){
  cfile << "int " << name << "[] = {";
  for(int i=0; i<v.size(); ++i){
    if(i!=0) cfile << ",";
    cfile << v[i];
  }
  cfile << "};" << endl;
}

int FXInternal::printSparsity(std::ostream &stream, const CRSSparsity& sp, std::map<const void*,int>& sparsity_index){
  // Get the current number of patterns before looking for it
  size_t num_patterns_before = sparsity_index.size();

  // Get index of the pattern
  const void* h = static_cast<const void*>(sp.get());
  int& ind = sparsity_index[h];

  // Generate it if it does not exist
  if(sparsity_index.size() > num_patterns_before){
    // Add at the end
    ind = num_patterns_before;
    
    // Compact version of the sparsity pattern
    std::vector<int> sp_compact = sp_compress(sp);
      
    // Give it a name
    stringstream name;
    name << "s" << ind;
    
    // Print to file
    printVector(stream,name.str(),sp_compact);
    
    // Separate with an empty line
    stream << endl;
  }

  return ind;
}

} // namespace CasADi

