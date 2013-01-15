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

%{
#include "symbolic/fx/fx.hpp"
#include "symbolic/fx/sx_function.hpp"
#include "symbolic/fx/mx_function.hpp"
#include "symbolic/fx/linear_solver.hpp"
#include "symbolic/fx/implicit_function.hpp"
#include "symbolic/fx/integrator.hpp"
#include "symbolic/fx/simulator.hpp"
#include "symbolic/fx/control_simulator.hpp"
#include "symbolic/fx/nlp_solver.hpp"
#include "symbolic/fx/qp_solver.hpp"
#include "symbolic/fx/ocp_solver.hpp"
#include "symbolic/fx/sdp_solver.hpp"
#include "symbolic/fx/external_function.hpp"
#include "symbolic/fx/parallelizer.hpp"
#include "symbolic/fx/c_function.hpp"
#include "symbolic/fx/fx_tools.hpp"
#include "symbolic/fx/xfunction_tools.hpp"
%}

#ifdef SWIGOCTAVE
%rename(__paren__) indexed_one_based;
#endif

#ifdef SWIGPYTHON
%rename(__getitem__) indexed_zero_based;
#endif

%include "symbolic/fx/fx.hpp"
%include "symbolic/fx/sx_function.hpp"
%include "symbolic/fx/mx_function.hpp"
%include "symbolic/fx/linear_solver.hpp"
%include "symbolic/fx/implicit_function.hpp"
%include "symbolic/fx/integrator.hpp"
%include "symbolic/fx/simulator.hpp"
%include "symbolic/fx/control_simulator.hpp"
%include "symbolic/fx/nlp_solver.hpp"
%include "symbolic/fx/qp_solver.hpp"
%include "symbolic/fx/ocp_solver.hpp"
%include "symbolic/fx/sdp_solver.hpp"
%include "symbolic/fx/external_function.hpp"
%include "symbolic/fx/parallelizer.hpp"
%include "symbolic/fx/c_function.hpp"
%include "symbolic/fx/fx_tools.hpp"
%include "symbolic/fx/xfunction_tools.hpp"

%template(IntegratorVector) std::vector<CasADi::Integrator>;
%template(Pair_FX_FX) std::pair<CasADi::FX,CasADi::FX>;

%{
#include "integration/rk_integrator.hpp"
%}

%include "integration/rk_integrator.hpp"
