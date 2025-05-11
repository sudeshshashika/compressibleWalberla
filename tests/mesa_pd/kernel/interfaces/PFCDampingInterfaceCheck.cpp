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
//! \file
//! \author Sebastian Eibl <sebastian.eibl@fau.de>
//
//======================================================================================================================

//======================================================================================================================
//
//  THIS FILE IS GENERATED - PLEASE CHANGE THE TEMPLATE !!!
//
//======================================================================================================================

#include <mesa_pd/data/IAccessor.h>
#include <mesa_pd/kernel/PFCDamping.h>

#include <core/UniqueID.h>

#include <map>

namespace walberla {
namespace mesa_pd {

class Accessor : public data::IAccessor
{
public:
   ~Accessor() override = default;
   const walberla::mesa_pd::Vec3& getLinearVelocity(const size_t /*p_idx*/) const {return linearVelocity_;}
   void setLinearVelocity(const size_t /*p_idx*/, const walberla::mesa_pd::Vec3& v) { linearVelocity_ = v;}
   
   const walberla::mesa_pd::Vec3& getForce(const size_t /*p_idx*/) const {return force_;}
   void setForce(const size_t /*p_idx*/, const walberla::mesa_pd::Vec3& v) { force_ = v;}
   
   const walberla::mesa_pd::data::particle_flags::FlagT& getFlags(const size_t /*p_idx*/) const {return flags_;}
   
   const walberla::mesa_pd::Vec3& getAngularVelocity(const size_t /*p_idx*/) const {return angularVelocity_;}
   void setAngularVelocity(const size_t /*p_idx*/, const walberla::mesa_pd::Vec3& v) { angularVelocity_ = v;}
   
   const walberla::mesa_pd::Vec3& getTorque(const size_t /*p_idx*/) const {return torque_;}
   void setTorque(const size_t /*p_idx*/, const walberla::mesa_pd::Vec3& v) { torque_ = v;}
   

   id_t getInvalidUid() const {return UniqueID<int>::invalidID();}
   size_t getInvalidIdx() const {return std::numeric_limits<size_t>::max();}
   /**
   * @brief Returns the index of particle specified by uid.
   * @param uid unique id of the particle to be looked up
   * @return the index of the particle or std::numeric_limits<size_t>::max() if the particle is not found
   */
   size_t uidToIdx(const id_t& /*uid*/) const {return 0;}
   size_t size() const { return 1; }
private:
   walberla::mesa_pd::Vec3 linearVelocity_;
   walberla::mesa_pd::Vec3 force_;
   walberla::mesa_pd::data::particle_flags::FlagT flags_;
   walberla::mesa_pd::Vec3 angularVelocity_;
   walberla::mesa_pd::Vec3 torque_;
};

template void kernel::PFCDamping::operator()(const size_t p_idx1, Accessor& ac) const;

} //namespace mesa_pd
} //namespace walberla