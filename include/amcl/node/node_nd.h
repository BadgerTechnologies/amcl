/*
 *  Copyright (C) 2020 Badger Technologies, LLC
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef AMCL_NODE_NODE_ND_H
#define AMCL_NODE_NODE_ND_H

#include "amcl/AMCLConfig.h"
#include "pf/pf_vector.h"

namespace amcl
{

class NodeND
{
public:
  virtual void reconfigure(AMCLConfig& config) = 0;
  virtual void globalLocalizationCallback() = 0;
  virtual double scorePose(const PFVector& p) = 0;
};

}  // namespace amcl

#endif // AMCL_NODE_NODE_ND_H
