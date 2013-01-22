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
from casadi import *

def color(A):
  print "="*80
  print "Original:"
  print repr(IMatrix(A,1))
  print "Colored: "
  print repr(IMatrix(A.unidirectionalColoring(),1))

A = sp_diag(5)
color(A)
#! One direction needed to capture all
color(sp_dense(5,10))
#! We need 5 directions.
#! The colored response reads: each row corresponds to a direction;
#! each column correspond to a row of the original matrix.

color(A+sp_triplet(5,5,[0],[4]))
#! First 4 rows can be taken together, the fifth row is taken seperately
color(A+sp_triplet(5,5,[4],[0]))
#! First 4 rows can be taken together, the fifth row is taken seperately

color(A+sp_triplet(5,5,[0]*5,range(5)))
#! The first row is taken seperately.
#! The remainding rows are lumped together in one direction.

color(A+sp_triplet(5,5,range(5),[0]*5))
#! We need 5 directions.

#! Next, we look at starColoring

def color(A):
  print "="*80
  print "Original:"
  print repr(IMatrix(A,1))
  print "Star colored: "
  print repr(IMatrix(A.starColoring(1),1))
  
color(A)
#! One direction needed to capture all

color(sp_dense(5,5))
#! We need 5 directions.

color(A+sp_triplet(5,5,[0]*5,range(5))+sp_triplet(5,5,range(5),[0]*5))
#! The first row/col is taken seperately.
#! The remainding rows/cols are lumped together in one direction.


