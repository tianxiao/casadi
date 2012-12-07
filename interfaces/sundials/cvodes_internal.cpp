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

#include "cvodes_internal.hpp"
#include "symbolic/stl_vector_tools.hpp"
#include "symbolic/fx/linear_solver_internal.hpp"
#include "symbolic/fx/mx_function.hpp"
#include "symbolic/sx/sx_tools.hpp"
#include "symbolic/mx/mx_tools.hpp"

using namespace std;
namespace CasADi{

CVodesInternal* CVodesInternal::clone() const{
  // Return a deep copy
  CVodesInternal* node = new CVodesInternal(f_,g_);
  node->setOption(dictionary());
  node->jac_ = jac_;
  node->linsol_ = linsol_;
  return node;
}

CVodesInternal::CVodesInternal(const FX& f, const FX& g) : SundialsInternal(f,g){
  addOption("linear_multistep_method",          OT_STRING,              "bdf",          "Integrator scheme","bdf|adams");
  addOption("nonlinear_solver_iteration",       OT_STRING,              "newton",       "","newton|functional");
  addOption("fsens_all_at_once",                OT_BOOLEAN,             true,           "Calculate all right hand sides of the sensitivity equations at once");
  addOption("disable_internal_warnings",        OT_BOOLEAN,             false,          "Disable CVodes internal warning messages");
  addOption("monitor",                          OT_STRINGVECTOR,        GenericType(),  "", "res|resB|resQB|reset|psetupB", true);
    
  mem_ = 0;

  x0_ = x_ = q_ = 0;
  rx0_ = rx_ = rq_ = 0;

  isInitAdj_ = false;
  disable_internal_warnings_ = false;
}

void CVodesInternal::freeCVodes(){
  if(mem_) { CVodeFree(&mem_); mem_ = 0;}

  // Forward integration
  if(x0_) { N_VDestroy_Serial(x0_); x0_ = 0; }
  if(x_) { N_VDestroy_Serial(x_); x_ = 0; }
  if(q_) { N_VDestroy_Serial(q_); q_ = 0; }
  
  // Backward integration
  if(rx0_) { N_VDestroy_Serial(rx0_); rx0_ = 0; }
  if(rx_)  { N_VDestroy_Serial(rx_);  rx_  = 0; }
  if(rq_)  { N_VDestroy_Serial(rq_);  rq_  = 0; }
  
  // Sensitivities of the forward integration
  for(vector<N_Vector>::iterator it=xF0_.begin(); it != xF0_.end(); ++it)   if(*it) { N_VDestroy_Serial(*it); *it=0;}
  for(vector<N_Vector>::iterator it=xF_.begin(); it != xF_.end(); ++it)     if(*it) { N_VDestroy_Serial(*it); *it=0;}
  for(vector<N_Vector>::iterator it=qF_.begin(); it != qF_.end(); ++it)   if(*it) { N_VDestroy_Serial(*it); *it=0;}
}

CVodesInternal::~CVodesInternal(){
  freeCVodes();
}

void CVodesInternal::updateNumSens(bool recursive){
  // Not supported re-initalization needed
  init();
}

void CVodesInternal::init(){
  log("CVodesInternal::init","begin");
  
  // Free memory if already initialized
  if(isInit()) freeCVodes();

  // Initialize the base classes
  SundialsInternal::init();

  // Read options
  monitor_rhsB_  = monitored("resB");
  monitor_rhs_   = monitored("res");
  monitor_rhsQB_ = monitored("resQB");
  
  // Get the number of forward and adjoint directions
  nfdir_f_ = f_.getOption("number_of_fwd_dir");

  // Sundials return flag
  int flag;

  if(getOption("linear_multistep_method")=="adams")  lmm_ = CV_ADAMS;
  else if(getOption("linear_multistep_method")=="bdf") lmm_ = CV_BDF;
  else throw CasadiException("Unknown linear multistep method");

  if(getOption("nonlinear_solver_iteration")=="newton") iter_ = CV_NEWTON;
  else if(getOption("nonlinear_solver_iteration")=="functional") iter_ = CV_FUNCTIONAL;
  else throw CasadiException("Unknown nonlinear solver iteration");

  // Create CVodes memory block
  mem_ = CVodeCreate(lmm_,iter_);
  if(mem_==0) throw CasadiException("CVodeCreate: Creation failed");

  // Allocate n-vectors for ivp
  x0_ = N_VMake_Serial(nx_,input(INTEGRATOR_X0).ptr());
  x_ = N_VMake_Serial(nx_,output(INTEGRATOR_XF).ptr());

  // Disable internal warning messages?
  disable_internal_warnings_ = getOption("disable_internal_warnings");

  // Set error handler function
  flag = CVodeSetErrHandlerFn(mem_, ehfun_wrapper, this);
  if(flag != CV_SUCCESS) cvodes_error("CVodeSetErrHandlerFn",flag);

  // Initialize CVodes
  double t0 = 0;
  flag = CVodeInit(mem_, rhs_wrapper, t0, x0_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeInit",flag);

  // Set tolerances
  flag = CVodeSStolerances(mem_, reltol_, abstol_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeInit",flag);

  // Maximum number of steps
  CVodeSetMaxNumSteps(mem_, getOption("max_num_steps").toInt());
  if(flag != CV_SUCCESS) cvodes_error("CVodeSetMaxNumSteps",flag);
  
  // attach a linear solver
  switch(linsol_f_){
    case SD_DENSE:
      initDenseLinearSolver();
      break;
    case SD_BANDED:
      initBandedLinearSolver();
      break;
    case SD_ITERATIVE:
      initIterativeLinearSolver();
      break;
    case SD_USER_DEFINED:
      initUserDefinedLinearSolver();
      break;
  }
      
  // Set user data
  flag = CVodeSetUserData(mem_,this);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeSetUserData",flag);

  // Quadrature equations
  if(nq_>0){
    // Allocate n-vectors for quadratures
    q_ = N_VMake_Serial(nq_,output(INTEGRATOR_QF).ptr());

    // Initialize quadratures in CVodes
    N_VConst(0.0, q_);
    flag = CVodeQuadInit(mem_, rhsQ_wrapper, q_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeQuadInit",flag);
    
    // Should the quadrature errors be used for step size control?
    if(getOption("quad_err_con").toInt()){
      flag = CVodeSetQuadErrCon(mem_, true);
      if(flag != CV_SUCCESS) cvodes_error("IDASetQuadErrCon",flag);
      
      // Quadrature error tolerances
      flag = CVodeQuadSStolerances(mem_, reltol_, abstol_); // TODO: vector absolute tolerances
      if(flag != CV_SUCCESS) cvodes_error("CVodeQuadSStolerances",flag);
    }
  }
  
    // Forward sensitivity problem
    if(nfdir_>0){
      // Allocate n-vectors
      xF0_.resize(nfdir_,0);
      xF_.resize(nfdir_,0);
      for(int i=0; i<nfdir_; ++i){
        xF0_[i] = N_VMake_Serial(nx_,fwdSeed(INTEGRATOR_X0,i).ptr());
        xF_[i] = N_VMake_Serial(nx_,fwdSens(INTEGRATOR_XF,i).ptr());
      }

      // Allocate n-vectors for quadratures
      if(nq_>0){
        qF_.resize(nfdir_,0);
        for(int i=0; i<nfdir_; ++i){
          qF_[i] = N_VMake_Serial(nq_,fwdSens(INTEGRATOR_QF,i).ptr());
        }
      }
      
    // Calculate all forward sensitivity right hand sides at once?
    bool all_at_once = getOption("fsens_all_at_once");
      
    // Get the sensitivity method
    if(getOption("sensitivity_method")=="simultaneous") ism_ = CV_SIMULTANEOUS;
    else if(getOption("sensitivity_method")=="staggered") ism_ = all_at_once ? CV_STAGGERED : CV_STAGGERED1;
    else throw CasadiException("CVodes: Unknown sensitivity method");
  
    // Initialize forward sensitivities
    if(finite_difference_fsens_){
      // Use finite differences to calculate the residual in the forward sensitivity equations
      if(all_at_once){
        flag = CVodeSensInit(mem_,nfdir_,ism_,0,getPtr(xF0_));
        if(flag != CV_SUCCESS) cvodes_error("CVodeSensInit",flag);
      } else {
        flag = CVodeSensInit1(mem_,nfdir_,ism_,0,getPtr(xF0_));
        if(flag != CV_SUCCESS) cvodes_error("CVodeSensInit1",flag);
      }
      
      // Pass pointer to parameters
      flag = CVodeSetSensParams(mem_,input(INTEGRATOR_P).ptr(),0,0);
      if(flag != CV_SUCCESS) cvodes_error("CVodeSetSensParams",flag);

      //  CVodeSetSensDQMethod

    } else {
      if(all_at_once){
        // Use AD to calculate the residual in the forward sensitivity equations
        flag = CVodeSensInit(mem_,nfdir_,ism_,rhsS_wrapper,getPtr(xF0_));
        if(flag != CV_SUCCESS) cvodes_error("CVodeSensInit",flag);
      } else {
        flag = CVodeSensInit1(mem_,nfdir_,ism_,rhsS1_wrapper,getPtr(xF0_));
        if(flag != CV_SUCCESS) cvodes_error("CVodeSensInit",flag);
      }
    }
    
    // Set tolerances
    vector<double> fsens_abstol(nfdir_,fsens_abstol_);
    
    flag = CVodeSensSStolerances(mem_,fsens_reltol_,getPtr(fsens_abstol));
    if(flag != CV_SUCCESS) cvodes_error("CVodeSensSStolerances",flag);
    
    // Set optional inputs
    bool errconS = getOption("fsens_err_con");
    flag = CVodeSetSensErrCon(mem_, errconS);
    if(flag != CV_SUCCESS) cvodes_error("CVodeSetSensErrCon",flag);
    
    // Quadrature equations
    if(nq_>0){
      for(vector<N_Vector>::iterator it=qF_.begin(); it!=qF_.end(); ++it) N_VConst(0.0,*it);
      flag = CVodeQuadSensInit(mem_, rhsQS_wrapper, getPtr(qF_));
      if(flag != CV_SUCCESS) cvodes_error("CVodeQuadSensInit",flag);

      // Set tolerances
      flag = CVodeQuadSensSStolerances(mem_,fsens_reltol_,getPtr(fsens_abstol));
      if(flag != CV_SUCCESS) cvodes_error("CVodeQuadSensSStolerances",flag);
    }
  } // enable fsens
    
  // Adjoint sensitivity problem
  if(!g_.isNull()){
    
    // Allocate n-vectors for backward integration
//     if(nfdir_>0){
//       rx_ = N_VNew_Serial((1+nfdir_)*nrx_);
//       rq_ = N_VNew_Serial((1+nfdir_)*nrq_);
//     } else {
      rx0_ = N_VMake_Serial(nrx_,input(INTEGRATOR_RX0).ptr());
      rx_ = N_VMake_Serial(nrx_,output(INTEGRATOR_RXF).ptr());
      rq_ = N_VMake_Serial(nrq_,output(INTEGRATOR_RQF).ptr());
//     }
    
    // Get the number of steos per checkpoint
    int Nd = getOption("steps_per_checkpoint");

    // Get the interpolation type
    int interpType;
    if(getOption("interpolation_type")=="hermite")
      interpType = CV_HERMITE;
    else if(getOption("interpolation_type")=="polynomial")
      interpType = CV_POLYNOMIAL;
    else throw CasadiException("\"interpolation_type\" must be \"hermite\" or \"polynomial\"");
      
    // Initialize adjoint sensitivities
    flag = CVodeAdjInit(mem_, Nd, interpType);
    if(flag != CV_SUCCESS) cvodes_error("CVodeAdjInit",flag);
          
    isInitAdj_ = false;
  }
}


void CVodesInternal::initAdj(){
  
  // Create backward problem (use the same lmm and iter)
  int flag = CVodeCreateB(mem_, lmm_, iter_, &whichB_);
  if(flag != CV_SUCCESS) cvodes_error("CVodeCreateB",flag);
  
  // Initialize the backward problem
  double tB0 = tf_;  
  flag = CVodeInitB(mem_, whichB_, rhsB_wrapper, tB0, rx0_);
  if(flag != CV_SUCCESS) cvodes_error("CVodeInitB",flag);
//   flag = CVodeInitBS(mem_, whichB_, rhsBS_wrapper, tB0, rx0_); // NOTE: Would be needed for forward sensitivities of the backward problem
//   if(flag != CV_SUCCESS) cvodes_error("CVodeInitBS",flag);

  // Set tolerances
  flag = CVodeSStolerancesB(mem_, whichB_, reltolB_, abstolB_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeSStolerancesB",flag);

  // User data
  flag = CVodeSetUserDataB(mem_, whichB_, this);
  if(flag != CV_SUCCESS) cvodes_error("CVodeSetUserDataB",flag);

  // attach linear solver to backward problem
  switch(linsol_g_){
    case SD_DENSE:
      initDenseLinearSolverB();
      break;
    case SD_BANDED:
      initBandedLinearSolverB();
      break;
    case SD_ITERATIVE:
      initIterativeLinearSolverB();
      break;
    case SD_USER_DEFINED:
      initUserDefinedLinearSolverB();
      break;
  }

  // Quadratures for the backward problem
  N_VConst(0.0, rq_);
  flag = CVodeQuadInitB(mem_,whichB_,rhsQB_wrapper,rq_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeQuadInitB",flag);
  
  if(getOption("quad_err_con").toInt()){
    flag = CVodeSetQuadErrConB(mem_, whichB_,true);
    if(flag != CV_SUCCESS) cvodes_error("CVodeSetQuadErrConB",flag);
      
    flag = CVodeQuadSStolerancesB(mem_, whichB_, reltolB_, abstolB_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeQuadSStolerancesB",flag);
  }
  
  // Mark initialized
  isInitAdj_ = true;
}

void CVodesInternal::rhs(double t, const double* x, double* xdot){
  log("CVodesInternal::rhs","begin");

  // Get time
  time1 = clock();

  // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(x,DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

  if(monitor_rhs_) {
    cout << "t       = " << t << endl;
    cout << "x       = " << f_.input(DAE_X) << endl;
    cout << "p       = " << f_.input(DAE_P) << endl;
  }
    // Evaluate
  f_.evaluate();

  if(monitor_rhs_) {
    cout << "xdot       = " << f_.output(DAE_ODE)<< endl;
  }
    
  // Get results
  f_.getOutput(xdot);

  // Log time
  time2 = clock();
  t_res += double(time2-time1)/CLOCKS_PER_SEC;

  log("CVodesInternal::rhs","end");

}

int CVodesInternal::rhs_wrapper(double t, N_Vector x, N_Vector xdot, void *user_data){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhs(t,NV_DATA_S(x),NV_DATA_S(xdot));
    return 0;
  } catch(exception& e){
    cerr << "rhs failed: " << e.what() << endl;
    return 1;
  }
}
  
void CVodesInternal::reset(int nsens, int nsensB, int nsensB_store){
  // Reset the base classes
  SundialsInternal::reset(nsens,nsensB,nsensB_store);
  
  if(monitored("reset")){
    cout << "initial state: " << endl;
    cout << "p = " << input(INTEGRATOR_P) << endl;
    cout << "x0 = " << input(INTEGRATOR_X0) << endl;
  }

  // Reset timers
  t_res = t_fres = t_jac = t_lsolve = t_lsetup_jac = t_lsetup_fac = 0;
  
  // Re-initialize
  int flag = CVodeReInit(mem_, t0_, x0_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeReInit",flag);
  
  // Re-initialize quadratures
  if(nq_>0){
    N_VConst(0.0,q_);
    flag = CVodeQuadReInit(mem_, q_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeQuadReInit",flag);
  }
  
  // Re-initialize sensitivities
  if(nsens>0){
    flag = CVodeSensReInit(mem_,ism_,getPtr(xF0_));
    if(flag != CV_SUCCESS) cvodes_error("CVodeSensReInit",flag);
    
    if(nq_>0){
      for(vector<N_Vector>::iterator it=qF_.begin(); it!=qF_.end(); ++it) N_VConst(0.0,*it);
      flag = CVodeQuadSensReInit(mem_, getPtr(qF_));
      if(flag != CV_SUCCESS) cvodes_error("CVodeQuadSensReInit",flag);
    }
  } else {
    // Turn of sensitivities
    flag = CVodeSensToggleOff(mem_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeSensToggleOff",flag);
  }
  
  // Re-initialize backward integration
  if(nrx_>0){
    flag = CVodeAdjReInit(mem_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeAdjReInit",flag);
  }
  
  // Set the stop time of the integration -- don't integrate past this point
  if(stop_at_end_) setStopTime(tf_);
}

void CVodesInternal::integrate(double t_out){
  log("CVODES::integrate begin");
  int flag;
    
  // tolerance
  double ttol = 1e-9;
  if(fabs(t_-t_out)<ttol){
    return;
  }
  if(nrx_>0){
    flag = CVodeF(mem_, t_out, x_, &t_, CV_NORMAL,&ncheck_);
    if(flag!=CV_SUCCESS && flag!=CV_TSTOP_RETURN) cvodes_error("CVodeF",flag);
    
  } else {
    flag = CVode(mem_, t_out, x_, &t_, CV_NORMAL);
    if(flag!=CV_SUCCESS && flag!=CV_TSTOP_RETURN) cvodes_error("CVode",flag);
  }
  
  if(nq_>0){
    double tret;
    flag = CVodeGetQuad(mem_, &tret, q_);
    if(flag!=CV_SUCCESS) cvodes_error("CVodeGetQuad",flag);
  }
  
  if(nsens_>0){
    // Get the sensitivities
    flag = CVodeGetSens(mem_, &t_, getPtr(xF_));
    if(flag != CV_SUCCESS) cvodes_error("CVodeGetSens",flag);
    
    if(nq_>0){
      double tret;
      flag = CVodeGetQuadSens(mem_, &tret, getPtr(qF_));
      if(flag != CV_SUCCESS) cvodes_error("CVodeGetQuadSens",flag);
    }
  }

  
  // Print statistics
  if(getOption("print_stats")) printStats(std::cout);
  
  log("CVODES::integrate end");
}

void CVodesInternal::resetB(){
  int flag;
  
  if(isInitAdj_){
    
    
    
    flag = CVodeReInitB(mem_, whichB_, tf_, rx0_);
    if(flag != CV_SUCCESS) cvodes_error("CVodeReInitB",flag);

    N_VConst(0.0,rq_);
    flag = CVodeQuadReInitB(mem_,whichB_,rq_);
    if(flag!=CV_SUCCESS) cvodes_error("CVodeQuadReInitB",flag);
    
  } else {
    // Initialize the adjoint integration
    initAdj();
  }
}

void CVodesInternal::integrateB(double t_out){
  int flag;
  
  // Integrate backward to t_out
  flag = CVodeB(mem_, t_out, CV_NORMAL);
  if(flag<CV_SUCCESS) cvodes_error("CVodeB",flag);

  // Get the sensitivities
  double tret;
  flag = CVodeGetB(mem_, whichB_, &tret, rx_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeGetB",flag);

  flag = CVodeGetQuadB(mem_, whichB_, &tret, rq_);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeGetQuadB",flag);
}

void CVodesInternal::printStats(std::ostream &stream) const{
  long nsteps, nfevals, nlinsetups, netfails;
  int qlast, qcur;
  double hinused, hlast, hcur, tcur;
  int flag = CVodeGetIntegratorStats(mem_, &nsteps, &nfevals,&nlinsetups, &netfails, &qlast, &qcur,&hinused, &hlast, &hcur, &tcur);
  if(flag!=CV_SUCCESS) cvodes_error("CVodeGetIntegratorStats",flag);

  // Get the number of right hand side evaluations in the linear solver
  long nfevals_linsol=0;
  switch(linsol_f_){
    case SD_DENSE:
    case SD_BANDED:
      flag = CVDlsGetNumRhsEvals(mem_, &nfevals_linsol);
      if(flag!=CV_SUCCESS) cvodes_error("CVDlsGetNumRhsEvals",flag);
      break;
    case SD_ITERATIVE:
      flag = CVSpilsGetNumRhsEvals(mem_, &nfevals_linsol);
      if(flag!=CV_SUCCESS) cvodes_error("CVSpilsGetNumRhsEvals",flag);
      break;
    default:
      nfevals_linsol = 0;
  }
  
  stream << "number of steps taken by CVODES:          " << nsteps << std::endl;
  stream << "number of calls to the user's f function: " << (nfevals + nfevals_linsol) << std::endl;
  stream << "   step calculation:                      " << nfevals << std::endl;
  stream << "   linear solver:                         " << nfevals_linsol << std::endl;
  stream << "number of calls made to the linear solver setup function: " << nlinsetups << std::endl;
  stream << "number of error test failures: " << netfails << std::endl;
  stream << "method order used on the last internal step: " << qlast << std::endl;
  stream << "method order to be used on the next internal step: " << qcur << std::endl;
  stream << "actual value of initial step size: " << hinused << std::endl;
  stream << "step size taken on the last internal step: " << hlast << std::endl;
  stream << "step size to be attempted on the next internal step: " << hcur << std::endl;
  stream << "current internal time reached: " << tcur << std::endl;
  stream << std::endl;

  stream << "number of checkpoints stored: " << ncheck_ << endl;
  stream << std::endl;
  
  stream << "Time spent in the ODE residual: " << t_res << " s." << endl;
  stream << "Time spent in the forward sensitivity residual: " << t_fres << " s." << endl;
  stream << "Time spent in the jacobian function or jacobian times vector function: " << t_jac << " s." << endl;
  stream << "Time spent in the linear solver solve function: " << t_lsolve << " s." << endl;
  stream << "Time spent to generate the jacobian in the linear solver setup function: " << t_lsetup_jac << " s." << endl;
  stream << "Time spent to factorize the jacobian in the linear solver setup function: " << t_lsetup_fac << " s." << endl;
  stream << std::endl;

#if 0
  // Quadrature
  if(ops.quadrature && ocp.hasFunction(LTERM)){
      long nfQevals, nQetfails;
      flag = CVodeGetQuadStats(cvode_mem_[k], &nfQevals, &nQetfails);  
      if(flag != CV_SUCCESS) throw "Error in CVodeGetQuadStats";

      stream << "Quadrature: " << std::endl;
      stream << "number of calls made to the user's quadrature right-hand side function: " << nfQevals << std::endl;
      stream << "number of local error test failures due to quadrature variables: " <<  nQetfails << std::endl;
      stream << std::endl;
}
#endif
}
  
map<int,string> CVodesInternal::calc_flagmap(){
  map<int,string> f;
  f[CV_SUCCESS] = "CV_SUCCESS";
  f[CV_TSTOP_RETURN] = "CV_TSTOP_RETURN";
  f[CV_ROOT_RETURN] = "CV_ROOT_RETURN";
  f[CV_WARNING] = "CV_WARNING";
  f[CV_WARNING] = "CV_WARNING";
  f[CV_TOO_MUCH_WORK] = "CV_TOO_MUCH_WORK";
  f[CV_TOO_MUCH_ACC] = "CV_TOO_MUCH_ACC";
  f[CV_ERR_FAILURE] = "CV_ERR_FAILURE";
  f[CV_CONV_FAILURE] = "CV_CONV_FAILURE";
  f[CV_LINIT_FAIL] = "CV_LINIT_FAIL";
  f[CV_LSETUP_FAIL] = "CV_LSETUP_FAIL";
  f[CV_LSOLVE_FAIL] = "CV_LSOLVE_FAIL";
  f[CV_RHSFUNC_FAIL] = "CV_RHSFUNC_FAIL";
  f[CV_FIRST_RHSFUNC_ERR] = "CV_FIRST_RHSFUNC_ERR";
  f[CV_UNREC_RHSFUNC_ERR] = "CV_UNREC_RHSFUNC_ERR";
  f[CV_RTFUNC_FAIL] = "CV_RTFUNC_FAIL";
  f[CV_MEM_FAIL] = "CV_MEM_FAIL";
  f[CV_ILL_INPUT] = "CV_ILL_INPUT";
  f[CV_NO_MALLOC] = "CV_NO_MALLOC";
  f[CV_BAD_K] = "CV_BAD_K";
  f[CV_BAD_T] = "CV_BAD_T";
  f[CV_BAD_DKY] = "CV_BAD_DKY";
  f[CV_TOO_CLOSE] = "CV_TOO_CLOSE";
  f[CV_QRHSFUNC_FAIL] = "CV_QRHSFUNC_FAIL";
  f[CV_FIRST_QRHSFUNC_ERR] = "CV_FIRST_QRHSFUNC_ERR";
  f[CV_REPTD_QRHSFUNC_ERR] = "CV_REPTD_QRHSFUNC_ERR";
  f[CV_UNREC_QRHSFUNC_ERR] = "CV_UNREC_QRHSFUNC_ERR";
  f[CV_NO_SENS ] = "CV_NO_SENS ";
  f[CV_SRHSFUNC_FAIL] = "CV_SRHSFUNC_FAIL";
  return f;
}
  
map<int,string> CVodesInternal::flagmap = CVodesInternal::calc_flagmap();

void CVodesInternal::cvodes_error(const string& module, int flag){
  // Find the error
  map<int,string>::const_iterator it = flagmap.find(flag);
  
  stringstream ss;
  if(it == flagmap.end()){
    ss << "Unknown error (" << flag << ") from module \"" << module << "\".";
  } else {
    ss << "Module \"" << module << "\" returned flag \"" << it->second << "\".";
  }
  ss << " Consult Cvodes documentation.";
  casadi_error(ss.str());
}
  
void CVodesInternal::ehfun_wrapper(int error_code, const char *module, const char *function, char *msg, void *user_data){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->ehfun(error_code,module,function,msg);        
  } catch(exception& e){
    cerr << "ehfun failed: " << e.what() << endl;
  }
}
  
void CVodesInternal::ehfun(int error_code, const char *module, const char *function, char *msg){
  if(!disable_internal_warnings_){
    cerr << msg << endl;
  }
}

void CVodesInternal::rhsS(int Ns, double t, N_Vector x, N_Vector xdot, N_Vector *xF, N_Vector *xdotF, N_Vector tmp1, N_Vector tmp2){
  casadi_assert(Ns==nfdir_);

  // Record the current cpu time
  time1 = clock();
  
    // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(NV_DATA_S(x),DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

   // Calculate the forward sensitivities, nfdir_f_ directions at a time
   for(int j=0; j<nfdir_; j += nfdir_f_){
     for(int dir=0; dir<nfdir_f_ && j+dir<nfdir_; ++dir){
       // Pass forward seeds 
       f_.fwdSeed(DAE_T,dir).setZero();
       f_.setFwdSeed(NV_DATA_S(xF[j+dir]),DAE_X,dir);
       f_.setFwdSeed(fwdSeed(INTEGRATOR_P,j+dir),DAE_P,dir);
     }

     // Evaluate the AD forward algorithm
     f_.evaluate(nfdir_f_,0);
      
     // Get the output seeds
     for(int dir=0; dir<nfdir_f_ && j+dir<nfdir_; ++dir){
       f_.getFwdSens(NV_DATA_S(xdotF[j+dir]),DAE_ODE,dir);
     }
   }
  
  // Record timings
  time2 = clock();
  t_fres += double(time2-time1)/CLOCKS_PER_SEC;
}

int CVodesInternal::rhsS_wrapper(int Ns, double t, N_Vector x, N_Vector xdot, N_Vector *xF, N_Vector *xdotF, void *user_data, N_Vector tmp1, N_Vector tmp2){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsS(Ns,t,x,xdot,xF,xdotF,tmp1,tmp2);
    return 0;
  } catch(exception& e){
    cerr << "fs failed: " << e.what() << endl;
    return 1;
  }
}

void CVodesInternal::rhsS1(int Ns, double t, N_Vector x, N_Vector xdot, int iS, N_Vector xF, N_Vector xdotF, N_Vector tmp1, N_Vector tmp2){
  casadi_assert(Ns==nfdir_);
  
    // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(NV_DATA_S(x),DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

  // Pass forward seeds
  f_.fwdSeed(DAE_T).setZero();
  f_.setFwdSeed(NV_DATA_S(xF),DAE_X);
  f_.setFwdSeed(fwdSeed(INTEGRATOR_P,iS),DAE_P);
    
  // Evaluate the AD forward algorithm
  f_.evaluate(1,0);
  
  // Get the fwd sensitivities
  f_.getFwdSens(NV_DATA_S(xdotF));
}

int CVodesInternal::rhsS1_wrapper(int Ns, double t, N_Vector x, N_Vector xdot, int iS, N_Vector xF, N_Vector xdotF, void *user_data, N_Vector tmp1, N_Vector tmp2){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsS1(Ns,t,x,xdot,iS,xF,xdotF,tmp1,tmp2);
    return 0;
  } catch(exception& e){
    cerr << "fs failed: " << e.what() << endl;
    return 1;
  }
}

int CVodesInternal::rhsQ_wrapper(double t, N_Vector x, N_Vector qdot, void *user_data){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsQ(t,NV_DATA_S(x),NV_DATA_S(qdot));
    return 0;
  } catch(exception& e){
    cerr << "rhsQ failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::rhsQ(double t, const double* x, double* qdot){
  // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(x,DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

  // Evaluate
  f_.evaluate();
    
  // Get results
  f_.getOutput(qdot,DAE_QUAD);
}

void CVodesInternal::rhsQS(int Ns, double t, N_Vector x, N_Vector *xF, N_Vector qdot, N_Vector *qdotF, N_Vector tmp1, N_Vector tmp2){
  casadi_assert(Ns==nfdir_);
  
  // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(NV_DATA_S(x),DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

  for(int i=0; i<nfdir_; ++i){
    // Pass forward seeds
    f_.fwdSeed(DAE_T).setZero();
    f_.setFwdSeed(NV_DATA_S(xF[i]),DAE_X);
    f_.setFwdSeed(fwdSeed(INTEGRATOR_P,i),DAE_P);

    // Evaluate the AD forward algorithm
    f_.evaluate(1,0);
      
    // Get the forward sensitivities
    f_.getFwdSens(NV_DATA_S(qdotF[i]),DAE_QUAD);
  }
}

int CVodesInternal::rhsQS_wrapper(int Ns, double t, N_Vector x, N_Vector *xF, N_Vector qdot, N_Vector *qdotF, void *user_data, N_Vector tmp1, N_Vector tmp2){
try{
//    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    if(!this_){
      // SUNDIALS BUG!!!
      for(int i=0; i<Ns; ++i) N_VConst(0.0,qdotF[i]);
      return 0;
    }
    this_->rhsQS(Ns,t,x,xF,qdot,qdotF,tmp1,tmp2);
    return 0;
  } catch(exception& e){
    cerr << "rhsQS failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::rhsB(double t, const double* x, const double *rx, double* rxdot){
  log("CVodesInternal::rhsB","begin");
  
  // Pass inputs
  g_.setInput(&t,RDAE_T);
  g_.setInput(x,RDAE_X);
  g_.setInput(input(INTEGRATOR_P),RDAE_P);
  g_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  g_.setInput(rx,RDAE_RX);

  if(monitor_rhsB_){
    cout << "t       = " << t << endl;
    cout << "x       = " << g_.input(RDAE_X) << endl;
    cout << "p       = " << g_.input(RDAE_P) << endl;
    cout << "rx      = " << g_.input(RDAE_RX) << endl;
    cout << "rp      = " << g_.input(RDAE_RP) << endl;
  }
  
  // Evaluate
  g_.evaluate();

  // Save to output
  g_.getOutput(rxdot,RDAE_ODE);

  if(monitor_rhsB_){
    cout << "xdotB = " << g_.output(RDAE_ODE) << endl;
  }
  
  // Negate (note definition of g)
  for(int i=0; i<nrx_; ++i)
    rxdot[i] *= -1;

  log("CVodesInternal::rhsB","end");
}

void CVodesInternal::rhsBS(double t, N_Vector x, N_Vector *xF, N_Vector rx, N_Vector rxdot){
  
  // Pass input
  g_.setInput(&t,RDAE_T);
  g_.setInput(NV_DATA_S(x),RDAE_X);
  g_.setInput(input(INTEGRATOR_P),RDAE_P);
  g_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  
  // Pass backward state
  const double *rx_data = NV_DATA_S(rx);
  
  // Pass backward state
  g_.setInput(rx_data,RDAE_RX); rx_data += nrx_;
  
  // Pass forward seeds 
  for(int dir=0; dir<nfdir_; ++dir){
    g_.fwdSeed(RDAE_T,dir).setZero();
    g_.setFwdSeed(rx_data,RDAE_RX,dir); rx_data += nrx_;
    g_.setFwdSeed(fwdSeed(INTEGRATOR_P,dir),RDAE_P,dir);
    g_.setFwdSeed(fwdSeed(INTEGRATOR_RP,dir),RDAE_RP,dir);
    g_.setFwdSeed(NV_DATA_S(xF[dir]),RDAE_X,dir);
  }

  // Evaluate the AD forward algorithm
  g_.evaluate(nfdir_,0);
    
  // Right hand side
  double *rxdot_data = NV_DATA_S(rxdot);

  // Get the backward right hand side
  g_.getOutput(rxdot_data,RDAE_ODE); rxdot_data += nrx_;
  
  // Get forward sensitivities
  for(int dir=0; dir<nfdir_; ++dir){
    g_.getFwdSens(rxdot_data,RDAE_ODE,dir); rxdot_data += nrx_;
  }
}
  
int CVodesInternal::rhsB_wrapper(double t, N_Vector x, N_Vector rx, N_Vector rxdot, void *user_data){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsB(t,NV_DATA_S(x),NV_DATA_S(rx),NV_DATA_S(rxdot));
    return 0;
  } catch(exception& e){
    cerr << "rhsB failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::rhsBS_wrapper(double t, N_Vector x, N_Vector *xF, N_Vector xB, N_Vector xdotB, void *user_data){
try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsBS(t,x,xF,xB,xdotB);
    return 0;
  } catch(exception& e){
    cerr << "rhsBS failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::rhsQB_wrapper(double t, N_Vector x, N_Vector rx, N_Vector rqdot, void *user_data){
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->rhsQB(t,NV_DATA_S(x),NV_DATA_S(rx),NV_DATA_S(rqdot));
    return 0;
  } catch(exception& e){
    cerr << "rhsQB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::rhsQB(double t, const double* x, const double* rx, double* rqdot){
  if(monitor_rhsQB_){
    cout << "CVodesInternal::rhsQB: begin" << endl;
  }

  // Pass inputs
  g_.setInput(&t,RDAE_T);
  g_.setInput(x,RDAE_X);
  g_.setInput(input(INTEGRATOR_P),RDAE_P);
  g_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  g_.setInput(rx,RDAE_RX);

  if(monitor_rhsB_){
    cout << "t       = " << t << endl;
    cout << "x       = " << g_.input(RDAE_X) << endl;
    cout << "p       = " << g_.input(RDAE_P) << endl;
    cout << "rx      = " << g_.input(RDAE_RX) << endl;
    cout << "rp      = " << g_.input(RDAE_RP) << endl;
  }
  
  // Evaluate
  g_.evaluate();

  // Save to output
  g_.getOutput(rqdot,RDAE_QUAD);

  if(monitor_rhsB_){
    cout << "qdotB = " << g_.output(RDAE_QUAD) << endl;
  }
  
  // Negate (note definition of g)
  for(int i=0; i<nrq_; ++i)
    rqdot[i] *= -1;
      
  if(monitor_rhsQB_){
    cout << "CVodesInternal::rhsQB: end" << endl;
  }
}

int CVodesInternal::jtimes_wrapper(N_Vector v, N_Vector Jv, double t, N_Vector x, N_Vector xdot, void *user_data, N_Vector tmp){
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->jtimes(v,Jv,t,x,xdot,tmp);
    return 0;
  } catch(exception& e){
    cerr << "jtimes failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::jtimesB_wrapper(N_Vector vB, N_Vector JvB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, void *user_data ,N_Vector tmpB) {
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->jtimesB(vB, JvB, t, x, xB, xdotB, tmpB);
    return 0;
  } catch(exception& e){
    cerr << "jtimes failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::jtimes(N_Vector v, N_Vector Jv, double t, N_Vector x, N_Vector xdot, N_Vector tmp){
  log("IdasInternal::jtimes","begin");
  // Get time
  time1 = clock();

  // Pass input
  f_.setInput(&t,DAE_T);
  f_.setInput(NV_DATA_S(x),DAE_X);
  f_.setInput(input(INTEGRATOR_P),DAE_P);

  // Pass input seeds
  f_.fwdSeed(DAE_T).setZero();
  f_.setFwdSeed(NV_DATA_S(v),DAE_X);
  f_.setFwdSeed(0.0,DAE_P);
  
  // Evaluate
  f_.evaluate(1,0);

  // Get the output seeds
  f_.getFwdSens(NV_DATA_S(Jv),DAE_ODE);
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  
  log("IdasInternal::jtimes","end");
}

void CVodesInternal::jtimesB(N_Vector vB, N_Vector JvB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, N_Vector tmpB) {
  log("IdasInternal::jtimesB","begin");
  // Get time
  time1 = clock();

  // Pass input
  g_.setInput(&t,RDAE_T);
  g_.setInput(NV_DATA_S(x),RDAE_X);
  g_.setInput(input(INTEGRATOR_P),RDAE_P);
  g_.setInput(NV_DATA_S(xB),RDAE_RX);
  g_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  
  // Pass input seeds
  g_.fwdSeed(RDAE_T).setZero();
  g_.fwdSeed(RDAE_X).setZero();
  g_.fwdSeed(RDAE_P).setZero();
  g_.setFwdSeed(NV_DATA_S(vB),RDAE_RX);
  g_.fwdSeed(RDAE_RP).setZero();
  
  // Evaluate
  g_.evaluate(1,0);

  // Get the output seeds
  g_.getFwdSens(NV_DATA_S(JvB),DAE_ODE);
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  log("IdasInternal::jtimesB","end");
}

int CVodesInternal::djac_wrapper(long N, double t, N_Vector x, N_Vector xdot, DlsMat Jac, void *user_data,N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->djac(N, t, x, xdot, Jac, tmp1, tmp2, tmp3);
    return 0;
  } catch(exception& e){
    cerr << "djac failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::djacB_wrapper(long NeqB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, DlsMat JacB, void *user_data, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B) {
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->djacB(NeqB, t, x, xB, xdotB, JacB, tmp1B, tmp2B, tmp3B);
    return 0;
  } catch(exception& e){
    cerr << "djacB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::djac(long N, double t, N_Vector x, N_Vector xdot, DlsMat Jac, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  log("IdasInternal::djac","begin");
  
  // Get time
  time1 = clock();

  // Pass inputs to the jacobian function
  jac_.setInput(&t,DAE_T);
  jac_.setInput(NV_DATA_S(x),DAE_X);
  jac_.setInput(f_.input(DAE_P),DAE_P);
  jac_.setInput(1.0,DAE_NUM_IN);
  jac_.setInput(0.0,DAE_NUM_IN+1);

  // Evaluate
  jac_.evaluate();
  
  // Get sparsity and non-zero elements
  const vector<int>& rowind = jac_.output().rowind();
  const vector<int>& col = jac_.output().col();
  const vector<double>& val = jac_.output().data();

  // Loop over rows
  for(int i=0; i<rowind.size()-1; ++i){
    // Loop over non-zero entries
    for(int el=rowind[i]; el<rowind[i+1]; ++el){
      // Get column
      int j = col[el];
      
      // Set the element
      DENSE_ELEM(Jac,i,j) = val[el];
    }
  }
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  
  log("IdasInternal::djac","wnd");
}

void CVodesInternal::djacB(long NeqB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, DlsMat JacB, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B) {
  log("IdasInternal::djacB","begin");
  // Get time
  time1 = clock();

  // Pass inputs to the jacobian function
  jacB_.setInput(&t,RDAE_T);
  jacB_.setInput(NV_DATA_S(x),RDAE_X);
  jacB_.setInput(input(INTEGRATOR_P),DAE_P);
  jacB_.setInput(NV_DATA_S(xB),RDAE_RX);
  jacB_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  jacB_.setInput(-1.0,RDAE_NUM_IN);
  jacB_.setInput(0.0,RDAE_NUM_IN+1);

  // Evaluate
  jacB_.evaluate();
  
  // Get sparsity and non-zero elements
  const vector<int>& rowind = jacB_.output().rowind();
  const vector<int>& col = jacB_.output().col();
  const vector<double>& val = jacB_.output().data();

  // Loop over rows
  for(int i=0; i<rowind.size()-1; ++i){
    // Loop over non-zero entries
    for(int el=rowind[i]; el<rowind[i+1]; ++el){
      // Get column
      int j = col[el];
      
      // Set the element
      DENSE_ELEM(JacB,i,j) = val[el];
    }
  }
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  log("IdasInternal::djacB","end");
}

int CVodesInternal::bjac_wrapper(long N, long mupper, long mlower, double t, N_Vector x, N_Vector xdot, DlsMat Jac, void *user_data,     
                        N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->bjac(N, mupper, mlower, t, x, xdot, Jac, tmp1, tmp2, tmp3);
    return 0;
  } catch(exception& e){
    cerr << "bjac failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::bjacB_wrapper(long NeqB, long mupperB, long mlowerB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, DlsMat JacB, void *user_data, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B) {
  try{
    casadi_assert(user_data);
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    this_->bjacB(NeqB, mupperB, mlowerB, t, x, xB, xdotB, JacB, tmp1B, tmp2B, tmp3B);
    return 0;
  } catch(exception& e){
    cerr << "bjacB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::bjac(long N, long mupper, long mlower, double t, N_Vector x, N_Vector xdot, DlsMat Jac, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  log("IdasInternal::bjac","begin");

  // Get time
  time1 = clock();

  // Pass inputs to the jacobian function
  jac_.setInput(&t,DAE_T);
  jac_.setInput(NV_DATA_S(x),DAE_X);
  jac_.setInput(f_.input(DAE_P),DAE_P);
  jac_.setInput(1.0,DAE_NUM_IN);
  jac_.setInput(0.0,DAE_NUM_IN+1);

  // Evaluate
  jac_.evaluate();
  
  // Get sparsity and non-zero elements
  const vector<int>& rowind = jac_.output().rowind();
  const vector<int>& col = jac_.output().col();
  const vector<double>& val = jac_.output().data();

  // Loop over rows
  for(int i=0; i<rowind.size()-1; ++i){
    // Loop over non-zero entries
    for(int el=rowind[i]; el<rowind[i+1]; ++el){
      // Get column
      int j = col[el];
      
      // Set the element
      if(i-j>=-mupper && i-j<=mlower)
        BAND_ELEM(Jac,i,j) = val[el];
    }
  }
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  
  log("IdasInternal::bjac","end");
}

void CVodesInternal::bjacB(long NeqB, long mupperB, long mlowerB, double t, N_Vector x, N_Vector xB, N_Vector xdotB, DlsMat JacB, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B) {
  log("IdasInternal::bjacB","begin");
  
  // Get time
  time1 = clock();

  // Pass inputs to the jacobian function
  jacB_.setInput(&t,RDAE_T);
  jacB_.setInput(NV_DATA_S(x),RDAE_X);
  jacB_.setInput(input(INTEGRATOR_P),DAE_P);
  jacB_.setInput(NV_DATA_S(xB),RDAE_RX);
  jacB_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  jacB_.setInput(-1.0,DAE_NUM_IN);
  jacB_.setInput(0.0,DAE_NUM_IN+1);

  // Evaluate
  jacB_.evaluate();
  
  // Get sparsity and non-zero elements
  const vector<int>& rowind = jacB_.output().rowind();
  const vector<int>& col = jacB_.output().col();
  const vector<double>& val = jacB_.output().data();

  // Loop over rows
  for(int i=0; i<rowind.size()-1; ++i){
    // Loop over non-zero entries
    for(int el=rowind[i]; el<rowind[i+1]; ++el){
      // Get column
      int j = col[el];
      
      // Set the element
      if(i-j>=-mupperB && i-j<=mlowerB)
        BAND_ELEM(JacB,i,j) = val[el];
    }
  }
  
  // Log time duration
  time2 = clock();
  t_jac += double(time2-time1)/CLOCKS_PER_SEC;
  
  log("IdasInternal::bjacB","end");
}

void CVodesInternal::setStopTime(double tf){
  // Set the stop time of the integration -- don't integrate past this point
  int flag = CVodeSetStopTime(mem_, tf);
  if(flag != CV_SUCCESS) cvodes_error("CVodeSetStopTime",flag);
}

int CVodesInternal::psolve_wrapper(double t, N_Vector x, N_Vector xdot, N_Vector r, N_Vector z, double gamma, double delta, int lr, void *user_data, N_Vector tmp){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    casadi_assert(this_);
    this_->psolve(t, x, xdot, r, z, gamma, delta, lr, tmp);
    return 0;
  } catch(exception& e){
    cerr << "psolve failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::psolveB_wrapper(double t, N_Vector x, N_Vector xB, N_Vector xdotB, N_Vector rvecB, N_Vector zvecB, double gammaB, double deltaB, int lr, void *user_data, N_Vector tmpB){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    casadi_assert(this_);
    this_->psolveB(t, x, xB, xdotB, rvecB, zvecB, gammaB, deltaB, lr, tmpB);
    return 0;
  } catch(exception& e){
    cerr << "psolveB failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::psetup_wrapper(double t, N_Vector x, N_Vector xdot, booleantype jok, booleantype *jcurPtr, double gamma, void *user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    casadi_assert(this_);
    this_->psetup(t, x, xdot, jok, jcurPtr, gamma, tmp1, tmp2, tmp3);
    return 0;
  } catch(exception& e){
    cerr << "psetup failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::psetupB_wrapper(double t, N_Vector x, N_Vector xB, N_Vector xdotB, booleantype jokB, booleantype *jcurPtrB, double gammaB, void *user_data, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(user_data);
    casadi_assert(this_);
    this_->psetupB(t, x, xB, xdotB, jokB, jcurPtrB, gammaB, tmp1B, tmp2B, tmp3B);
    return 0;
  } catch(exception& e){
    cerr << "psetupB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::psolve(double t, N_Vector x, N_Vector xdot, N_Vector r, N_Vector z, double gamma, double delta, int lr, N_Vector tmp){
  // Get time
  time1 = clock();

  // Copy input to output, if necessary
  if(r!=z){
    N_VScale(1.0, r, z);
  }

  // Solve the (possibly factorized) system 
  casadi_assert(linsol_.output().size() == NV_LENGTH_S(z));
  linsol_.solve(NV_DATA_S(z),1);
  
  // Log time duration
  time2 = clock();
  t_lsolve += double(time2-time1)/CLOCKS_PER_SEC;
}

void CVodesInternal::psolveB(double t, N_Vector x, N_Vector xB, N_Vector xdotB, N_Vector rvecB, N_Vector zvecB, double gammaB, double deltaB, int lr, N_Vector tmpB) {
 // Get time
  time1 = clock();

  // Copy input to output, if necessary
  if(rvecB!=zvecB){
    N_VScale(1.0, rvecB, zvecB);
  }

  // Solve the (possibly factorized) system 
  casadi_assert(linsolB_.output().size() == NV_LENGTH_S(zvecB));
  linsolB_.solve(NV_DATA_S(zvecB),1);
  
  // Log time duration
  time2 = clock();
  t_lsolve += double(time2-time1)/CLOCKS_PER_SEC;
}

void CVodesInternal::psetup(double t, N_Vector x, N_Vector xdot, booleantype jok, booleantype *jcurPtr, double gamma, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3){
  log("IdasInternal::psetup","begin");
  // Get time
  time1 = clock();

  // Pass input to the jacobian function
  jac_.setInput(&t,DAE_T);
  jac_.setInput(NV_DATA_S(x),DAE_X);
  jac_.setInput(input(INTEGRATOR_P),DAE_P);
  jac_.setInput(-gamma,DAE_NUM_IN);
  jac_.setInput(1.0,DAE_NUM_IN+1);

  // Evaluate jacobian
  jac_.evaluate();
  
  // Log time duration
  time2 = clock();
  t_lsetup_jac += double(time2-time1)/CLOCKS_PER_SEC;

  // Pass non-zero elements, scaled by -gamma, to the linear solver
  linsol_.setInput(jac_.output(),0);

  // Prepare the solution of the linear system (e.g. factorize) -- only if the linear solver inherits from LinearSolver
  linsol_.prepare();

  // Log time duration
  time1 = clock();
  t_lsetup_fac += double(time1-time2)/CLOCKS_PER_SEC;
  
  log("IdasInternal::psetup","end");
}

void CVodesInternal::psetupB(double t, N_Vector x, N_Vector xB, N_Vector xdotB, booleantype jokB, booleantype *jcurPtrB, double gammaB, N_Vector tmp1B, N_Vector tmp2B, N_Vector tmp3B) {
  log("IdasInternal::psetupB","begin");
  // Get time
  time1 = clock();

  // Pass inputs to the jacobian function
  jacB_.setInput(&t,RDAE_T);
  jacB_.setInput(NV_DATA_S(x),RDAE_X);
  jacB_.setInput(input(INTEGRATOR_P),DAE_P);
  jacB_.setInput(NV_DATA_S(xB),RDAE_RX);
  jacB_.setInput(input(INTEGRATOR_RP),RDAE_RP);
  jacB_.setInput(gammaB,RDAE_NUM_IN); // FIXME? Is this right
  jacB_.setInput(1.0,RDAE_NUM_IN+1); // FIXME? Is this right

  if(monitored("psetupB")){
    cout << "RDAE_T    = " << t << endl;
    cout << "RDAE_X    = " << jacB_.input(RDAE_X) << endl;
    cout << "RDAE_P    = " << jacB_.input(RDAE_P) << endl;
    cout << "RDAE_RX    = " << jacB_.input(RDAE_RX) << endl;
    cout << "RDAE_RP    = " << jacB_.input(RDAE_RP) << endl;
    cout << "gamma = " << gammaB << endl;
  }
  
  // Evaluate jacobian
  jacB_.evaluate();

  if(monitored("psetupB")){
    cout << "psetupB = " << jacB_.output() << endl;
  }
  
  // Log time duration
  time2 = clock();
  t_lsetup_jac += double(time2-time1)/CLOCKS_PER_SEC;

  // Pass non-zero elements, scaled by -gamma, to the linear solver
  linsolB_.setInput(jacB_.output(),0);

  // Prepare the solution of the linear system (e.g. factorize) -- only if the linear solver inherits from LinearSolver
  linsolB_.prepare();

  // Log time duration
  time1 = clock();
  t_lsetup_fac += double(time1-time2)/CLOCKS_PER_SEC;
  log("IdasInternal::psetupB","end");
}

void CVodesInternal::lsetup(CVodeMem cv_mem, int convfail, N_Vector x, N_Vector xdot, booleantype *jcurPtr, N_Vector vtemp1, N_Vector vtemp2, N_Vector vtemp3){
  // Current time
  double t = cv_mem->cv_tn;

  // Scaling factor before J
  double gamma = cv_mem->cv_gamma;

  // Call the preconditioner setup function (which sets up the linear solver)
  psetup(t, x, xdot, FALSE, jcurPtr, gamma, vtemp1, vtemp2, vtemp3);
}

void CVodesInternal::lsetupB(double t, double gamma, int convfail, N_Vector x, N_Vector xB, N_Vector xdotB, booleantype *jcurPtr, N_Vector vtemp1, N_Vector vtemp2, N_Vector vtemp3) {
  // Call the preconditioner setup function (which sets up the linear solver)
  psetupB(t, x, xB, xdotB, FALSE, jcurPtr, gamma, vtemp1, vtemp2, vtemp3);
}

int CVodesInternal::lsetup_wrapper(CVodeMem cv_mem, int convfail, N_Vector x, N_Vector xdot, booleantype *jcurPtr, N_Vector vtemp1, N_Vector vtemp2, N_Vector vtemp3){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(cv_mem->cv_lmem);
    casadi_assert(this_);
    this_->lsetup(cv_mem, convfail, x, xdot, jcurPtr, vtemp1, vtemp2, vtemp3);
    return 0;
  } catch(exception& e){
    cerr << "lsetup failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::lsetupB_wrapper(CVodeMem cv_mem, int convfail, N_Vector x, N_Vector xdot, booleantype *jcurPtr, N_Vector vtemp1, N_Vector vtemp2, N_Vector vtemp3) {
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(cv_mem->cv_lmem);
    casadi_assert(this_);
    CVadjMem ca_mem;
    CVodeBMem cvB_mem; 
  
    int flag;

    // Current time
    double t = cv_mem->cv_tn; // TODO: is this correct?
    double gamma = cv_mem->cv_gamma;
    
    cv_mem = static_cast<CVodeMem>(cv_mem->cv_user_data);

    ca_mem = cv_mem->cv_adj_mem;
    cvB_mem = ca_mem->ca_bckpbCrt;

    // Get FORWARD solution from interpolation.
    flag = ca_mem->ca_IMget(cv_mem, t, ca_mem->ca_ytmp, NULL);
    if (flag != CV_SUCCESS) casadi_error("Could not interpolate forward states");
  
    this_->lsetupB(t, gamma, convfail, ca_mem->ca_ytmp, x, xdot, jcurPtr, vtemp1, vtemp2, vtemp3);
    return 0;
  } catch(exception& e){
    cerr << "lsetupB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::lsolve(CVodeMem cv_mem, N_Vector b, N_Vector weight, N_Vector x, N_Vector xdot){
  // Current time
  double t = cv_mem->cv_tn;

  // Scaling factor before J
  double gamma = cv_mem->cv_gamma;

  // Accuracy
  double delta = 0.0;
  
  // Left/right preconditioner
  int lr = 1;
  
  // Call the preconditioner solve function (which solves the linear system)
  psolve(t, x, xdot, b, b, gamma, delta, lr, 0);
}

void CVodesInternal::lsolveB(double t, double gamma, N_Vector b, N_Vector weight, N_Vector x, N_Vector xB, N_Vector xdotB) {
  // Accuracy
  double delta = 0.0;
  
  // Left/right preconditioner
  int lr = 1;
  
  // Call the preconditioner solve function (which solves the linear system)
  psolveB(t, x, xB, xdotB, b, b, gamma, delta, lr, 0);
}

int CVodesInternal::lsolve_wrapper(CVodeMem cv_mem, N_Vector b, N_Vector weight, N_Vector x, N_Vector xdot){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(cv_mem->cv_lmem);
    casadi_assert(this_);
    this_->lsolve(cv_mem, b, weight, x, xdot);
    return 0;
  } catch(exception& e){
    cerr << "lsolve failed: " << e.what() << endl;;
    return 1;
  }
}

int CVodesInternal::lsolveB_wrapper(CVodeMem cv_mem, N_Vector b, N_Vector weight, N_Vector x, N_Vector xdot){
  try{
    CVodesInternal *this_ = static_cast<CVodesInternal*>(cv_mem->cv_lmem);
    casadi_assert(this_);
    CVadjMem ca_mem;
    CVodeBMem cvB_mem; 
  
    int flag;

    // Current time
    double t = cv_mem->cv_tn; // TODO: is this correct?
    double gamma = cv_mem->cv_gamma;
    
    cv_mem = static_cast<CVodeMem>(cv_mem->cv_user_data);

    ca_mem = cv_mem->cv_adj_mem;
    cvB_mem = ca_mem->ca_bckpbCrt;

    // Get FORWARD solution from interpolation.
    flag = ca_mem->ca_IMget(cv_mem, t, ca_mem->ca_ytmp, NULL);
    if (flag != CV_SUCCESS) casadi_error("Could not interpolate forward states");
  
    this_->lsolveB(t, gamma, b, weight, ca_mem->ca_ytmp, x, xdot);
    return 0;
  } catch(exception& e){
    cerr << "lsolveB failed: " << e.what() << endl;;
    return 1;
  }
}

void CVodesInternal::initDenseLinearSolver(){
  int flag = CVDense(mem_, nx_);
  if(flag!=CV_SUCCESS) cvodes_error("CVDense",flag);
  if(exact_jacobian_){ 
    flag = CVDlsSetDenseJacFn(mem_, djac_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVDlsSetDenseJacFn",flag);
  }
}

void CVodesInternal::initBandedLinearSolver(){
  int flag = CVBand(mem_, nx_, getOption("upper_bandwidth").toInt(), getOption("lower_bandwidth").toInt());
  if(flag!=CV_SUCCESS) cvodes_error("CVBand",flag);
  if(exact_jacobian_){
    flag = CVDlsSetBandJacFn(mem_, bjac_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVDlsSetBandJacFn",flag);
  }
}
  
void CVodesInternal::initIterativeLinearSolver(){
  // Max dimension of the Krylov space
  int maxl = getOption("max_krylov");

  // Attach the sparse solver
  int flag;
  switch(itsol_f_){
    case SD_GMRES:
      flag = CVSpgmr(mem_, pretype_f_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVBand",flag);
      break;
    case SD_BCGSTAB:
      flag = CVSpbcg(mem_, pretype_f_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVSpbcg",flag);
      break;
    case SD_TFQMR:
      flag = CVSptfqmr(mem_, pretype_f_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVSptfqmr",flag);
      break;
  }
  
  // Attach functions for jacobian information
  if(exact_jacobian_){
    flag = CVSpilsSetJacTimesVecFn(mem_, jtimes_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVSpilsSetJacTimesVecFn",flag);      
  }
    
  // Add a preconditioner
  if(use_preconditioner_){
    // Make sure that a Jacobian has been provided
    if(jac_.isNull()) throw CasadiException("CVodesInternal::init(): No Jacobian has been provided.");

    // Make sure that a linear solver has been providided
    if(linsol_.isNull()) throw CasadiException("CVodesInternal::init(): No user defined linear solver has been provided.");

    // Pass to IDA
    flag = CVSpilsSetPreconditioner(mem_, psetup_wrapper, psolve_wrapper);
    if(flag != CV_SUCCESS) cvodes_error("CVSpilsSetPreconditioner",flag);
  }    
}
  
void CVodesInternal::initUserDefinedLinearSolver(){
  // Make sure that a Jacobian has been provided
  if(jac_.isNull()) throw CasadiException("CVodesInternal::initUserDefinedLinearSolver(): No Jacobian has been provided.");

  // Make sure that a linear solver has been providided
  if(linsol_.isNull()) throw CasadiException("CVodesInternal::initUserDefinedLinearSolver(): No user defined linear solver has been provided.");

  //  Set fields in the IDA memory
  CVodeMem cv_mem = static_cast<CVodeMem>(mem_);
  cv_mem->cv_lmem   = this;
  cv_mem->cv_lsetup = lsetup_wrapper;
  cv_mem->cv_lsolve = lsolve_wrapper;
  cv_mem->cv_setupNonNull = TRUE;
}

void CVodesInternal::initDenseLinearSolverB(){
  int flag = CVDenseB(mem_, whichB_, nrx_);
  if(flag!=CV_SUCCESS) cvodes_error("CVDenseB",flag);
  if(exact_jacobianB_){
    // Generate jacobians if not already provided
    if(jacB_.isNull()) jacB_ = getJacobian();
    if(!jacB_.isInit()) jacB_.init();
    
    // Pass to CVodes
    flag = CVDlsSetDenseJacFnB(mem_, whichB_, djacB_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVDlsSetDenseJacFnB",flag);
  }
}
  
void CVodesInternal::initBandedLinearSolverB(){
  int flag = CVBandB(mem_, whichB_, nrx_, getOption("upper_bandwidthB").toInt(), getOption("lower_bandwidthB").toInt());
  if(flag!=CV_SUCCESS) cvodes_error("CVBandB",flag);
  
  if(exact_jacobianB_){
    flag = CVDlsSetBandJacFnB(mem_, whichB_, bjacB_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVDlsSetBandJacFnB",flag);
  }
}
  
void CVodesInternal::initIterativeLinearSolverB(){
  int maxl = getOption("max_krylovB");
  int flag;
  switch(itsol_g_){
    case SD_GMRES:
      flag = CVSpgmrB(mem_, whichB_, pretype_g_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVSpgmrB",flag);
      break;
    case SD_BCGSTAB:
      flag = CVSpbcgB(mem_, whichB_, pretype_g_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVSpbcgB",flag);
      break;
    case SD_TFQMR:
      flag = CVSptfqmrB(mem_, whichB_, pretype_g_, maxl);
      if(flag!=CV_SUCCESS) cvodes_error("CVSptfqmrB",flag);
      break;
  }
  
  // Attach functions for jacobian information
  if(exact_jacobianB_){
    flag = CVSpilsSetJacTimesVecFnB(mem_, whichB_, jtimesB_wrapper);
    if(flag!=CV_SUCCESS) cvodes_error("CVSpilsSetJacTimesVecFnB",flag);      
  }
  
  // Add a preconditioner
  if(use_preconditionerB_){
    // Make sure that a Jacobian has been provided
    if(jacB_.isNull()) throw CasadiException("CVodesInternal::init(): No backwards Jacobian has been provided.");

    // Make sure that a linear solver has been providided
    if(linsolB_.isNull()) throw CasadiException("CVodesInternal::init(): No user defined backwards  linear solver has been provided.");

    // Pass to IDA
    flag = CVSpilsSetPreconditionerB(mem_, whichB_, psetupB_wrapper, psolveB_wrapper);
    if(flag != CV_SUCCESS) cvodes_error("CVSpilsSetPreconditionerB",flag);
  }
  
}

void CVodesInternal::initUserDefinedLinearSolverB(){
  // Make sure that a Jacobian has been provided
  if(jacB_.isNull()) throw CasadiException("CVodesInternal::initUserDefinedLinearSolverB(): No backwards Jacobian has been provided.");

  // Make sure that a linear solver has been providided
  if(linsolB_.isNull()) throw CasadiException("CVodesInternal::initUserDefinedLinearSolverB(): No user defined backward linear solver has been provided.");

  CVodeMem cv_mem = static_cast<CVodeMem>(mem_);
  CVadjMem ca_mem = cv_mem->cv_adj_mem;
  CVodeBMem cvB_mem = ca_mem->cvB_mem;
  cvB_mem->cv_lmem   = this;

  cvB_mem->cv_mem->cv_lmem = this;
  cvB_mem->cv_mem->cv_lsetup = lsetupB_wrapper;
  cvB_mem->cv_mem->cv_lsolve = lsolveB_wrapper;
  cvB_mem->cv_mem->cv_setupNonNull = TRUE;
}

void CVodesInternal::deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied){
  SundialsInternal::deepCopyMembers(already_copied);
  jac_ = deepcopy(jac_,already_copied);
}

template<typename FunctionType>
FunctionType CVodesInternal::getJacobianGen(){
  FunctionType f = shared_cast<FunctionType>(f_);
  casadi_assert(!f.isNull());
  
  // Get the Jacobian in the Newton iteration
  typename FunctionType::MatType c_x = FunctionType::MatType::sym("c_x");
  typename FunctionType::MatType c_xdot = FunctionType::MatType::sym("c_xdot");
  typename FunctionType::MatType jac = c_x*f.jac(DAE_X,DAE_ODE) + c_xdot*FunctionType::MatType::eye(nx_);
  
  // Jacobian function
  std::vector<typename FunctionType::MatType> jac_in = f.inputExpr();
  jac_in.push_back(c_x);
  jac_in.push_back(c_xdot);
  
  // Return generated function
  return FunctionType(jac_in,jac);
}

template<typename FunctionType>
FunctionType CVodesInternal::getJacobianGenB(){
  FunctionType g = shared_cast<FunctionType>(g_);
  casadi_assert(!g.isNull());
  
  // Get the Jacobian in the Newton iteration
  typename FunctionType::MatType c_x = FunctionType::MatType::sym("c_x");
  typename FunctionType::MatType c_xdot = FunctionType::MatType::sym("c_xdot");
  typename FunctionType::MatType jac = c_x*g.jac(RDAE_RX,RDAE_ODE) + c_xdot*FunctionType::MatType::eye(nrx_);
    
  // Jacobian function
  std::vector<typename FunctionType::MatType> jac_in = g.inputExpr();
  jac_in.push_back(c_x);
  jac_in.push_back(c_xdot);
  
  // return generated function
  return FunctionType(jac_in,jac);
}

FX CVodesInternal::getJacobianB(){
  if(is_a<SXFunction>(g_)){
    return getJacobianGenB<SXFunction>();
  } else if(is_a<MXFunction>(g_)){
    return getJacobianGenB<MXFunction>();
  } else {
    throw CasadiException("CVodesInternal::getJacobianB(): Not an SXFunction or MXFunction");
  }
}


FX CVodesInternal::getJacobian(){
  if(is_a<SXFunction>(f_)){
    return getJacobianGen<SXFunction>();
  } else if(is_a<MXFunction>(f_)){
    return getJacobianGen<MXFunction>();
  } else {
    throw CasadiException("CVodesInternal::getJacobian(): Not an SXFunction or MXFunction");
  }
}


} // namespace CasADi

