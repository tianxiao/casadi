#include <symbolic/fx/fx_tools.hpp>
#include <symbolic/mx/mx_tools.hpp>
#include <symbolic/sx/sx_tools.hpp>
#include <symbolic/matrix/matrix_tools.hpp>
#include <symbolic/stl_vector_tools.hpp>

#include <interfaces/ipopt/ipopt_solver.hpp>
#include <interfaces/sundials/cvodes_integrator.hpp>
#include <interfaces/sundials/idas_integrator.hpp>

#include <optimal_control/direct_multiple_shooting.hpp>

using namespace CasADi;
using namespace std;


int main(){
  //Final time (fixed)
  double tf = 10.0;

  // Infinity
  double inf = numeric_limits<double>::infinity();

  // Declare variables (use simple, efficient DAG)
  SX t("t"); //time
  SX x("x"), y("y"), u("u"), L("cost");
  
  // All states
  vector<SX> xx(3);  xx[0] = x;  xx[1] = y;  xx[2] = L;

  //ODE right hand side
  vector<SX> f(3);
  f[0] = (1 - y*y)*x - y + u;
  f[1] = x;
  f[2] = x*x + y*y + u*u;
  
  // DAE residual
  vector<SXMatrix> res_in = daeIn<SXMatrix>("x",xx, "p",u, "t",t);
  SXFunction res(res_in,daeOut<SXMatrix>("ode",f));
  
  Dictionary integrator_options;
  integrator_options["abstol"]=1e-8; //abs. tolerance
  integrator_options["reltol"]=1e-8; //rel. tolerance
  integrator_options["steps_per_checkpoint"]=500;
  integrator_options["stop_at_end"]=true;
//  integrator_options["calc_ic"]=true;
//  integrator_options["numeric_jacobian"]=true;
  
  //Numboer of shooting nodes
  int ns = 50;

  // Number of differential states
  int nx = 3;
  
  // Number of controls
  int nu = 1;
  
  // Mayer objective function
  Matrix<SX> xf = ssym("xf",nx,1);
  SXFunction mterm(xf, xf[nx-1]);

  // Create a multiple shooting discretization
  DirectMultipleShooting ms(res,mterm);
  ms.setOption("integrator",CVodesIntegrator::creator);
  //ms.setOption("integrator",IdasIntegrator::creator);
  ms.setOption("integrator_options",integrator_options);
  ms.setOption("number_of_grid_points",ns);
  ms.setOption("final_time",tf);
  ms.setOption("parallelization","openmp");
  
  // NLP solver
  ms.setOption("nlp_solver",IpoptSolver::creator);
  Dictionary nlp_solver_dict;
  nlp_solver_dict["tol"] = 1e-5;
  nlp_solver_dict["hessian_approximation"] = "limited-memory";
  nlp_solver_dict["max_iter"] = 100;
  nlp_solver_dict["linear_solver"] = "ma57";
  //  nlp_solver_dict["derivative_test"] = "first-order";
  //  nlp_solver_dict["verbose"] = true;
  ms.setOption("nlp_solver_options",nlp_solver_dict);
  
  ms.init();

  //Control bounds
  double u_min[] = {-0.75};
  double u_max[] = {1.0};
  double u_init[] = {0.0};
  
  for(int k=0; k<ns; ++k){
    copy(u_min,u_min+nu,ms.input(OCP_LBU).begin()+k*nu);
    copy(u_max,u_max+nu,ms.input(OCP_UBU).begin()+k*nu);
    copy(u_init,u_init+nu,ms.input(OCP_U_INIT).begin()+k*nu);
  }
  
  ms.input(OCP_LBX).setAll(-inf);
  ms.input(OCP_UBX).setAll(inf);
  ms.input(OCP_X_INIT).setAll(0);

  // Initial condition
  ms.input(OCP_LBX)(0,0) = ms.input(OCP_UBX)(0,0) = 0;
  ms.input(OCP_LBX)(1,0) = ms.input(OCP_UBX)(1,0) = 1;
  ms.input(OCP_LBX)(2,0) = ms.input(OCP_UBX)(2,0) = 0;

  // Final condition
  ms.input(OCP_LBX)(0,ns) = ms.input(OCP_UBX)(0,ns) = 0; 
  ms.input(OCP_LBX)(1,ns) = ms.input(OCP_UBX)(1,ns) = 0; 
  
  // Solve the problem
  ms.solve();

  cout << ms.output(OCP_X_OPT) << endl;
  cout << ms.output(OCP_U_OPT) << endl;
  
  return 0;
}



