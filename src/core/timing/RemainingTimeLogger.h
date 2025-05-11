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
//! \file RemainingTimeLogger.h
//! \ingroup core
//! \author Martin Bauer <martin.bauer@fau.de>
//
//======================================================================================================================

#pragma once

#include "core/logging/Logging.h"
#include "core/timing/Timer.h"
#include "core/DataTypes.h"

#include <iomanip>

namespace walberla
{
namespace timing
{

/***********************************************************************************************************************
 * \brief Functor that can be added to a time loop in order to print an estimated remaining runtime.
 *
 * \ingroup timing
 *
 Example Usage:
 \code
   timeloop.addFuncAfterTimeStep( RemainingTimeLogger( timeloop.getNrOfTimeSteps() ), "RemainingTimeLogger" );
 \endcode
 *
 **********************************************************************************************************************/
class RemainingTimeLogger
{
 public:
   RemainingTimeLogger(const uint_t nrTimesteps, const real_t logIntervalInSec = 10,
                       const int minOutputWidth = 8, const uint_t startTimestep = 0) :
   logIntervalInSec_(logIntervalInSec), timestep_(startTimestep), nrTimesteps_(nrTimesteps), minOutputWidth_(minOutputWidth)
   {
      WALBERLA_UNUSED(minOutputWidth_);
   }

   void operator()()
   {
      WALBERLA_ROOT_SECTION()
      {
         if (firstRun_)
         {
            timer_.start();
            firstRun_ = false;
            ++timestep_;
            return;
         }

         timer_.end();
         ++timestep_;

         timeSinceLastLog_ += real_c(timer_.last());

         if (timeSinceLastLog_ > logIntervalInSec_)
         {
            timeSinceLastLog_ = 0.0;

            uint_t const timeStepsRemaining = nrTimesteps_ - timestep_;

            real_t const remainingTime = real_c(timer_.average()) * real_c(timeStepsRemaining);
            WALBERLA_UNUSED(remainingTime);
            WALBERLA_LOG_INFO("Estimated Remaining Time: " << std::setw(minOutputWidth_) << std::right
                                                           << timing::timeToString(remainingTime));
         }
         timer_.start();
      }
   }

 private:
   WcTimer timer_;
   real_t timeSinceLastLog_{ 0.0 };
   real_t logIntervalInSec_;
   uint_t timestep_;
   uint_t nrTimesteps_;
   int minOutputWidth_;
   bool firstRun_{ true };
};

} // namespace timing
} // namespace walberla

namespace walberla
{
using timing::RemainingTimeLogger;
}