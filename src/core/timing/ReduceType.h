//======================================================================================================================
//
//  This file is part of waLBerla. waLBerla is free software: you can 
//  redistribute it and/or modify it under the terms of the GNU General Public
//  License as published by the Free Software Foundation, either version 3 of 
//  the License, or (at your option) any later version.
//  
//  waLBerla is distributed in the hope that it will be useful, but WITHOUT 
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//  for more details.
//  
//  You should have received a copy of the GNU General Public License along
//  with waLBerla (see COPYING.txt). If not, see <http://www.gnu.org/licenses/>.
//
//! \file ReduceType.h
//! \author Sebastian Eibl <sebastian.eibl@fau.de>
//
//======================================================================================================================

#pragma once

namespace walberla {
namespace timing {

/// The reduce type describes which values are reduced from each process
enum ReduceType
{
   /// Treats each process as one timing sample. For the timing sample the min value is used.
   REDUCE_MIN,
   /// Treats each process as one timing sample. For the timing sample the average value is used.
   REDUCE_AVG,
   /// Treats each process as one timing sample. For the timing sample the max value is used.
   REDUCE_MAX,
   /// Collects all timing samples from all processes and accumulates the data.
   /// The number of measurements afterwards are: nrOfProcesses*measurementsPerProcess
   REDUCE_TOTAL
};

} //namespace timing
} //namespace walberla
