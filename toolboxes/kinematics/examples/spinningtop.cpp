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

#include <iostream>
#include <fstream>
#include <string>

#include <kinetics.hpp>

using namespace std;

// CART PENDULUM
//
// This is a trivial example to get stated with the kinematics toolbox.
// Pendulum on a cart.

int main(int argc, char *argv[]){

try{

// ------------------------------
// Symbolic variable definitions|
// ------------------------------

SXMatrix t("t");			// Time
SXMatrix phi("phi"),theta("theta"),delta("delta");	// The states we will use
SXMatrix dphi("dx"),dtheta("dtheta"),ddelta("ddelta");

SXMatrix r("r");				// Position of CM
SXMatrix m("m");				// mass of spinning top
SXMatrix Ixx("Ixx"),Iyy("Iyy"),Izz("Izz");// inertia tensor components of spinning top
SXMatrix I; I(0,0)=Ixx;I(1,1)=Iyy;I(2,2)=Izz; // Inertia tensor in {1} coordinates

SXMatrix q; q<< phi << theta << delta;      // The time changing variables of our system
SXMatrix dq; dq<< dphi << dtheta << ddelta; // The derivatives of these variables


SXMatrix g("g");

// -------------------
// Frame definitions |
// -------------------

// We define three frames according to image spinningtop.svg
Frame f0("world frame",q,dq,t);
Frame f1("CM frame",f0,TRz(phi)*TRy(-theta)*tr(r,0,0)); 
Frame f2("rotating frame",f1,TRx(delta)); 

// -------------------
// Forces and moments|
// -------------------

// Forces working on the system
KinVec Fg(0,0,-m*g,0,f0); 			// Gravity working at CM
SXMatrix FRx("FRx"),FRy("FRy"),FRz("FRz");	// Reaction force components working on contact with ground
KinVec FR(FRx,FRy,FRz,0,f0); 			// Reaction force
KinVec M=cross(pos(f1,f0),FR);  		// Moment working on CM from reaction force of ground

 
// ----------
// Kinetics |
// ----------

// Kinetics - all in inertial frame
KinVec v=omega(f1,f0,f0);	// linear velocity
KinVec a=omega(f1,f0,f0);	// linear acceleration

KinVec w=omega(f2,f0,f1);	// rotational velocity
KinVec alpha=omegad(f2,f0,f1);	// rotational acceleration

// ---------------------
// Equations of motion |
// ---------------------

//  A) Newton approach
M-(I*alpha+cross(w,I*w));

FR = acc(f1,f0,f0)*m-Fg; // Note that the total force on the centre of mass is NOT zero


//  B) Lagrange-Equation approach approach
SXMatrix T=m*norm(v)*norm(v)/2+transpose(w)*I*w/2;
SXMatrix V=pos(f1,f0,f0)*g*m;

SXMatrix L=T-V;

L.jacobian(dq).der(t)-L.jacobian(q)=pos(f1,f0,f0).jacobian(q)*Fg;

// -------------
// Integration |
// -------------

} catch (const char * str){
  cerr << str << endl;
  return 1;
}
}
