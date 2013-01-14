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

#include "weak_ref.hpp"

using namespace std;

namespace CasADi{
  
  WeakRef::WeakRef(){
  }
    
  bool WeakRef::alive() const{
    return !isNull() && (*this)->raw_ != 0;
  }
    
  SharedObject WeakRef::shared(){
    SharedObject ret;
    if(!isNull() && (*this)->raw_ !=0){
      ret.assignNode((*this)->raw_);
    }
    return ret;
  }

  const WeakRefInternal* WeakRef::operator->() const{
    return static_cast<const WeakRefInternal*>(SharedObject::operator->());
  }

  WeakRefInternal* WeakRef::operator->(){
    return static_cast<WeakRefInternal*>(SharedObject::operator->());
  }

  WeakRef::WeakRef(SharedObject& shared){
    assignNode(shared.weak()->get());
  }

  WeakRef::operator SharedObject(){
    return shared();
  }
  
  WeakRef::WeakRef(SharedObjectNode* raw){
    assignNode(new WeakRefInternal(raw));
  }

  void WeakRef::kill(){
    (*this)->raw_ = 0;
  }

  WeakRefInternal::WeakRefInternal(SharedObjectNode* raw) : raw_(raw){
  }
  
  WeakRefInternal::~WeakRefInternal(){
  }
  
} // namespace CasADi

