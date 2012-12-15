#
#     This file is part of CasADi.
# 
#     CasADi -- A symbolic framework for dynamic optimization.
#     Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
# 
#     CasADi is free software; you can redistribute it and/or
#     modify it under the terms of the GNU Lesser General Public
#     License as published by the Free Software Foundation; either
#     version 3 of the License, or (at your option) any later version.
# 
#     CasADi is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#     Lesser General Public License for more details.
# 
#     You should have received a copy of the GNU Lesser General Public
#     License along with CasADi; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
# 
# 
# -*- coding: utf-8 -*-
from casadi import *

# Declare variables
x = ssym("x",2)

# Form the NLP objective
f = SXFunction([x],[x[0]**2 + x[1]**2])

# Form the NLP constraints
g = SXFunction([x],[x[0]+x[1]-10])

# Pick an NLP solver
#MySolver = IpoptSolver
#MySolver = WorhpSolver
MySolver = SQPMethod

# Allocate a solver
solver = MySolver(f,g)
if MySolver==SQPMethod:
  solver.setOption("qp_solver",QPOasesSolver)
  solver.setOption("qp_solver_options",{"printLevel":"none"})
solver.init()

# Set constraint bounds
solver.setInput(0.,NLP_LBG)

# Solve the NLP
solver.evaluate()

# Print solution
print "-----"
print "objective at solution = ", solver.output(NLP_COST)
print "primal solution = ", solver.output(NLP_X_OPT)
print "dual solution (x) = ", solver.output(NLP_LAMBDA_X)
print "dual solution (g) = ", solver.output(NLP_LAMBDA_G)
