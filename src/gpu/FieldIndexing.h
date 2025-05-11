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
//! \file FieldIndexing.h
//! \ingroup gpu
//! \author Martin Bauer <martin.bauer@fau.de>
//! \brief Indexing Scheme that executes all elements of inner coordinate within on thread block
//
//======================================================================================================================

#pragma once

#include "stencil/Directions.h"
#include "gpu/DeviceWrapper.h"

#include "FieldAccessor.h"

namespace walberla
{
namespace cell
{
class CellInterval;
}
} // namespace walberla

namespace walberla
{
namespace gpu
{

// Forward Declarations
template< typename T >
class GPUField;

/**
 * \brief Utility class to generate handles to the underlying device data of a \ref GPUField.
 *
 * Pre-calculate memory offsets of a \ref GPUField for a given slice,
 * cell interval, or the entire grid with or without the ghost layer,
 * and store them in a \ref FieldAccessor handle.
 * That handle is obtained by calling \ref gpuAccess().
 */
template< typename T >
class FieldIndexing
{
 public:
   //** Kernel call        ******************************************************************************************
   /*! \name Kernel call  */
   //@{
   dim3 blockDim() const { return blockDim_; }
   dim3 gridDim() const { return gridDim_; }

   const FieldAccessor< T >& gpuAccess() const { return gpuAccess_; }
   //@}
   //****************************************************************************************************************

   //** Creation        *********************************************************************************************
   /*! \name Creation  */
   //@{
   static FieldIndexing< T > interval(const GPUField< T >& f, const cell::CellInterval& ci, int fBegin = 0,
                                      int fEnd = 1);

   static FieldIndexing< T > xyz(const GPUField< T >& f);
   static FieldIndexing< T > withGhostLayerXYZ(const GPUField< T >& f, uint_t numGhostLayers);
   static FieldIndexing< T > ghostLayerOnlyXYZ(const GPUField< T >& f, uint_t thickness, stencil::Direction dir,
                                               bool fullSlice = false);
   static FieldIndexing< T > sliceBeforeGhostLayerXYZ(const GPUField< T >& f, uint_t thickness, stencil::Direction dir,
                                                      bool fullSlice = false);
   static FieldIndexing< T > sliceXYZ(const GPUField< T >& f, cell_idx_t distance, uint_t thickness,
                                      stencil::Direction dir, bool fullSlice = false);

   static FieldIndexing< T > allInner(const GPUField< T >& f);
   static FieldIndexing< T > allWithGhostLayer(const GPUField< T >& f);
   static FieldIndexing< T > all(const GPUField< T >& f, const cell::CellInterval& ci);
   //@}
   //****************************************************************************************************************

 protected:
   FieldIndexing(const GPUField< T >& field, dim3 _blockDim, dim3 _gridDim, const FieldAccessor< T > _gpuAccess);

   const GPUField< T >& field_;
   dim3 blockDim_;
   dim3 gridDim_;
   FieldAccessor< T > gpuAccess_;
};

} // namespace gpu
} // namespace walberla

#include "FieldIndexing.impl.h"
