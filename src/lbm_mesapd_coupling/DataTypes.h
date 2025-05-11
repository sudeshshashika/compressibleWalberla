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
//! \file DataTypes.h
//! \ingroup lbm_mesapd_coupling
//! \author Christoph Rettinger <christoph.rettinger@fau.de>
//
//======================================================================================================================

#pragma once

#include "core/DataTypes.h"

#include "field/GhostLayerField.h"

namespace walberla
{
namespace lbm_mesapd_coupling
{

/*
 * Typedefs specific to the lbm - mesa_pd coupling
 */
using ParticleField_T = walberla::GhostLayerField< walberla::id_t, 1 >;

namespace psm
{
// store the particle uid together with the overlap fraction
using ParticleAndVolumeFraction_T      = std::pair< id_t, real_t >;
using ParticleAndVolumeFractionField_T = GhostLayerField< std::vector< ParticleAndVolumeFraction_T >, 1 >;

} // namespace psm
} // namespace lbm_mesapd_coupling
} // namespace walberla
