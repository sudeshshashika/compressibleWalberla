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
//! \file Datatype.h
//! \ingroup mpi
//! \author Martin Bauer <martin.bauer@fau.de>
//
//======================================================================================================================

#pragma once

#include "MPIWrapper.h"
#include "core/Abort.h"


namespace walberla {
namespace mpi {


   //*******************************************************************************************************************
   /*! RAII class for MPI data types that commits and frees them
   *
   */
   //*******************************************************************************************************************
   class Datatype
   {
   public:
      Datatype() : mpiDatatype_( MPI_DATATYPE_NULL ) {}

      Datatype( MPI_Datatype datatype) : mpiDatatype_( datatype )
      {
         WALBERLA_MPI_SECTION() { MPI_Type_commit( &mpiDatatype_ ); }
      }

      explicit Datatype(const uint_t byteSize) : mpiDatatype_(MPI_DATATYPE_NULL)
      {
         WALBERLA_MPI_SECTION()
         {
            if (MPI_Type_contiguous(int_c(byteSize), MPI_BYTE, &mpiDatatype_) != MPI_SUCCESS)
            {
               WALBERLA_ABORT("MPI_Type_contiguous " << typeid(mpiDatatype_).name() << " failed.");
            }
            if (MPI_Type_commit(&mpiDatatype_) != MPI_SUCCESS)
            {
               WALBERLA_ABORT("MPI_Type_commit " << typeid(mpiDatatype_).name() << " failed.");
            }
         }
      }

      void init( MPI_Datatype datatype )
      {
         mpiDatatype_ = datatype;
         WALBERLA_MPI_SECTION() { MPI_Type_commit( &mpiDatatype_ ); }
      }

      ~Datatype() {
         WALBERLA_MPI_SECTION() { MPI_Type_free( & mpiDatatype_ ); }
      }

      operator MPI_Datatype() const {
         return mpiDatatype_;
      }

   protected:
      MPI_Datatype mpiDatatype_;
   };




} // namespace mpi
} // namespace walberla


