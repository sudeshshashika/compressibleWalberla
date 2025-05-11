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
//! \file multiphase.cpp
//! \author Markus Holzer <markus.holzer@fau.de>
//
//======================================================================================================================
#include "blockforest/Initialization.h"
#include "blockforest/communication/UniformDirectScheme.h"

#include "core/Environment.h"
#include "core/logging/Initialization.h"
#include "core/math/Constants.h"
#include "core/timing/RemainingTimeLogger.h"

#include "gpu/AddGPUFieldToStorage.h"
#include "gpu/DeviceSelectMPI.h"
#include "gpu/NVTX.h"
#include "gpu/ParallelStreams.h"
#include "gpu/communication/UniformGPUScheme.h"

#include "field/AddToStorage.h"
#include "field/FlagField.h"
#include "field/communication/PackInfo.h"
#include "field/vtk/VTKWriter.h"

#include "geometry/InitBoundaryHandling.h"
#include "geometry/mesh/TriangleMeshIO.h"

#include "lbm/PerformanceEvaluation.h"

#include "postprocessing/FieldToSurfaceMesh.h"

#include "python_coupling/CreateConfig.h"
#include "python_coupling/PythonCallback.h"
#include "python_coupling/export/FieldExports.h"

#include "timeloop/SweepTimeloop.h"

#include "InitializerFunctions.h"
#include "PythonExports.h"
#include "util.h"

//////////////////////////////
// INCLUDE GENERATED FILES //
////////////////////////////

#include "GenDefines.h"

////////////
// USING //
//////////

using namespace walberla;

using FlagField_T = FlagField< uint8_t >;

typedef gpu::GPUField< real_t > GPUField;
typedef gpu::GPUField< uint8_t > GPUField_int;

int main(int argc, char** argv)
{
   const mpi::Environment Env(argc, argv);
   gpu::selectDeviceBasedOnMpiRank();
   exportDataStructuresToPython();

   for (auto cfg = python_coupling::configBegin(argc, argv); cfg != python_coupling::configEnd(); ++cfg)
   {
      WALBERLA_MPI_WORLD_BARRIER()

      auto config = *cfg;
      logging::configureLogging(config);
      shared_ptr< StructuredBlockForest > blocks = blockforest::createUniformBlockGridFromConfig(config);

      ////////////////////////////
      // ADD DOMAIN PARAMETERS //
      //////////////////////////

      auto domainSetup                = config->getOneBlock("DomainSetup");
      Vector3< uint_t > cellsPerBlock = domainSetup.getParameter< Vector3< uint_t > >("cellsPerBlock", Vector3< int >(128, 1, 1));
      const bool tube                 = domainSetup.getParameter< bool >("tube", false);

      ////////////////////////////////////////
      // ADD GENERAL SIMULATION PARAMETERS //
      //////////////////////////////////////

      auto parameters                    = config->getOneBlock("Parameters");
      const std::string timeStepStrategy = parameters.getParameter< std::string >("timeStepStrategy", "normal");
      const uint_t timesteps             = parameters.getParameter< uint_t >("timesteps", uint_c(50));
      const real_t remainingTimeLoggerFrequency =
         parameters.getParameter< real_t >("remainingTimeLoggerFrequency", real_c(3.0));
      const uint_t scenario = parameters.getParameter< uint_t >("scenario", uint_c(1));
      Vector3< int > gpuBlockSize =
         parameters.getParameter< Vector3< int > >("gpuBlockSize", Vector3< int >(128, 1, 1));
      const bool cudaEnabledMpi = parameters.getParameter< bool >("cudaEnabledMpi", false);

      /////////////////////////
      // ADD DATA TO BLOCKS //
      ///////////////////////

      // CPU fields
      BlockDataID vel_field   = field::addToStorage< VelocityField_T >(blocks, "vel", real_c(0.0), field::fzyx);
      BlockDataID phase_field = field::addToStorage< PhaseField_T >(blocks, "phase", real_c(0.0), field::fzyx);
      // GPU fields
      const BlockDataID lb_phase_field_gpu = gpu::addGPUFieldToStorage< gpu::GPUField< real_t > >(
         blocks, "lb phase field on GPU", Stencil_phase_T::Size, field::fzyx, 1);
      const BlockDataID lb_velocity_field_gpu = gpu::addGPUFieldToStorage< gpu::GPUField< real_t > >(
         blocks, "lb velocity field on GPU", Stencil_hydro_T::Size, field::fzyx, 1);
      BlockDataID vel_field_gpu =
         gpu::addGPUFieldToStorage< VelocityField_T >(blocks, vel_field, "velocity field on GPU", true);
      BlockDataID phase_field_gpu =
         gpu::addGPUFieldToStorage< PhaseField_T >(blocks, phase_field, "phase field on GPU", true);
      // Flag field
      const BlockDataID flagFieldID     = field::addFlagFieldToStorage< FlagField_T >(blocks, "flag field");
      const BlockDataID flagFieldID_gpu = gpu::addGPUFieldToStorage< FlagField_T >(blocks, flagFieldID, "flag on GPU", true);

      auto physical_parameters     = config->getOneBlock("PhysicalParameters");
      const real_t density_liquid  = physical_parameters.getParameter< real_t >("density_liquid", real_c(1.0));
      const real_t density_gas     = physical_parameters.getParameter< real_t >("density_gas");
      const real_t surface_tension = physical_parameters.getParameter< real_t >("surface_tension");
      const real_t mobility        = physical_parameters.getParameter< real_t >("mobility");
      const real_t gravitational_acceleration =
         physical_parameters.getParameter< real_t >("gravitational_acceleration");
      const real_t relaxation_time_liquid = physical_parameters.getParameter< real_t >("relaxation_time_liquid");
      const real_t relaxation_time_gas    = physical_parameters.getParameter< real_t >("relaxation_time_gas");
      const real_t interface_thickness    = physical_parameters.getParameter< real_t >("interface_thickness");

      std::array< real_t, 3 > center_of_mass = { 0.0, 0.0, 0.0 };

      WALBERLA_LOG_INFO_ON_ROOT("initialization of the phase field")
      if (scenario == 1)
      {
         auto bubbleParameters                  = config->getOneBlock("Bubble");
         const Vector3< real_t > bubbleMidPoint = bubbleParameters.getParameter< Vector3< real_t > >("bubbleMidPoint");
         const real_t bubbleRadius              = bubbleParameters.getParameter< real_t >("bubbleRadius", real_c(20.0));
         const bool bubble                      = bubbleParameters.getParameter< bool >("bubble", true);
         initPhaseField_sphere(blocks, phase_field, bubbleRadius, bubbleMidPoint, bubble, interface_thickness);
      }
      else if (scenario == 2)
      {
         initPhaseField_RTI(blocks, phase_field, interface_thickness, tube);
      }
      else if (scenario == 3)
      {
         auto bubbleParameters     = config->getOneBlock("Bubble");
         const real_t bubbleRadius = bubbleParameters.getParameter< real_t >("bubbleRadius", real_c(20.0));
         init_bubble_field(blocks, phase_field, bubbleRadius);
      }
      else if (scenario == 4)
      {
         auto TorusParameters   = config->getOneBlock("Torus");
         const real_t midpoint  = TorusParameters.getParameter< real_t >("Donut_midpoint");
         const real_t height    = TorusParameters.getParameter< real_t >("Donut_h");
         const real_t diameter  = TorusParameters.getParameter< real_t >("Donut_D");
         const real_t donutTime = TorusParameters.getParameter< real_t >("DonutTime");
         init_Taylor_bubble(blocks, phase_field, diameter, height, donutTime, midpoint);
         center_of_mass[0] = real_c(cellsPerBlock[0]);
         center_of_mass[1] = real_c(midpoint);
         center_of_mass[2] = real_c(cellsPerBlock[2]);
      }

      WALBERLA_LOG_INFO_ON_ROOT("initialization of the phase field done")

      /////////////////
      // ADD SWEEPS //
      ///////////////

      pystencils::initialize_phase_field_distributions init_h(lb_phase_field_gpu, phase_field_gpu, vel_field_gpu,
                                                              interface_thickness);
      pystencils::initialize_velocity_based_distributions init_g(lb_velocity_field_gpu, vel_field_gpu);

      const pystencils::phase_field_LB_step phase_field_LB_step(flagFieldID_gpu, lb_phase_field_gpu, phase_field_gpu,
                                                          vel_field_gpu, mobility, interface_thickness, gpuBlockSize[0],
                                                          gpuBlockSize[1], gpuBlockSize[2]);

      const pystencils::hydro_LB_step hydro_LB_step(flagFieldID_gpu, lb_velocity_field_gpu, phase_field_gpu, vel_field_gpu,
                                              gravitational_acceleration, interface_thickness, density_liquid,
                                              density_gas, surface_tension, relaxation_time_liquid, relaxation_time_gas,
                                              gpuBlockSize[0], gpuBlockSize[1], gpuBlockSize[2]);

      ////////////////////////
      // ADD COMMUNICATION //
      //////////////////////
      const int streamLowPriority  = 0;
      const int streamHighPriority = 0;
      auto defaultStream     = gpu::StreamRAII::newPriorityStream(streamLowPriority);
      auto innerOuterStreams = gpu::ParallelStreams(streamHighPriority);

      auto UniformGPUSchemeVelocityDistributions =
         make_shared< gpu::communication::UniformGPUScheme< Stencil_hydro_T > >(blocks, cudaEnabledMpi);
      auto generatedPackInfo_velocity_based_distributions =
         make_shared< lbm::PackInfo_velocity_based_distributions >(lb_velocity_field_gpu);
      UniformGPUSchemeVelocityDistributions->addPackInfo(generatedPackInfo_velocity_based_distributions);
      auto Comm_velocity_based_distributions =
         std::function< void() >([&]() { UniformGPUSchemeVelocityDistributions->communicate(); });
      auto Comm_velocity_based_distributions_start =
         std::function< void() >([&]() { UniformGPUSchemeVelocityDistributions->startCommunication(); });
      auto Comm_velocity_based_distributions_wait =
         std::function< void() >([&]() { UniformGPUSchemeVelocityDistributions->wait(); });

      auto UniformGPUSchemePhaseField =
         make_shared< gpu::communication::UniformGPUScheme< Stencil_hydro_T > >(blocks, cudaEnabledMpi);
      auto generatedPackInfo_phase_field = make_shared< pystencils::PackInfo_phase_field >(phase_field_gpu);
      UniformGPUSchemePhaseField->addPackInfo(generatedPackInfo_phase_field);
      auto Comm_phase_field = std::function< void() >([&]() { UniformGPUSchemePhaseField->communicate(); });
      auto Comm_phase_field_start =
         std::function< void() >([&]() { UniformGPUSchemePhaseField->startCommunication(); });
      auto Comm_phase_field_wait = std::function< void() >([&]() { UniformGPUSchemePhaseField->wait(); });

      auto UniformGPUSchemePhaseFieldDistributions =
         make_shared< gpu::communication::UniformGPUScheme< Stencil_hydro_T > >(blocks, cudaEnabledMpi);
      auto generatedPackInfo_phase_field_distributions =
         make_shared< lbm::PackInfo_phase_field_distributions >(lb_phase_field_gpu);
      UniformGPUSchemePhaseFieldDistributions->addPackInfo(generatedPackInfo_phase_field_distributions);
      auto Comm_phase_field_distributions =
         std::function< void() >([&]() { UniformGPUSchemePhaseFieldDistributions->communicate(); });
      auto Comm_phase_field_distributions_start =
         std::function< void() >([&]() { UniformGPUSchemePhaseFieldDistributions->startCommunication(); });
      auto Comm_phase_field_distributions_wait =
         std::function< void() >([&]() { UniformGPUSchemePhaseFieldDistributions->wait(); });

      ////////////////////////
      // BOUNDARY HANDLING //
      //////////////////////

      const FlagUID fluidFlagUID("Fluid");
      const FlagUID wallFlagUID("NoSlip");

      auto boundariesConfig = config->getBlock("Boundaries");
      if (boundariesConfig)
      {
         geometry::initBoundaryHandling< FlagField_T >(*blocks, flagFieldID, boundariesConfig);
         if (tube)
         {
            const real_t inner_radius      = domainSetup.getParameter< real_t >("inner_radius", real_c(0));
            const real_t eccentricity      = domainSetup.getParameter< real_t >("ratio", real_c(0));
            const real_t start_transition  = domainSetup.getParameter< real_t >("start_transition", real_c(60));
            const real_t length_transition = domainSetup.getParameter< real_t >("length_transition", real_c(10));
            const bool eccentricity_or_pipe_ration =
               domainSetup.getParameter< bool >("eccentricity_or_pipe_ration", true);
            initTubeWithCylinder(blocks, flagFieldID, wallFlagUID, inner_radius, eccentricity, start_transition,
                                 length_transition, eccentricity_or_pipe_ration);
         }
         geometry::setNonBoundaryCellsToDomain< FlagField_T >(*blocks, flagFieldID, fluidFlagUID);
      }
      gpu::fieldCpy< GPUField_int, FlagField_T >(blocks, flagFieldID_gpu, flagFieldID);

      lbm::phase_field_LB_NoSlip phase_field_LB_NoSlip(blocks, lb_phase_field_gpu);
      lbm::hydro_LB_NoSlip hydro_LB_NoSlip(blocks, lb_velocity_field_gpu);
      pystencils::ContactAngle contact_angle(blocks, phase_field_gpu, interface_thickness);

      phase_field_LB_NoSlip.fillFromFlagField< FlagField_T >(blocks, flagFieldID, wallFlagUID, fluidFlagUID);
      hydro_LB_NoSlip.fillFromFlagField< FlagField_T >(blocks, flagFieldID, wallFlagUID, fluidFlagUID);
      contact_angle.fillFromFlagField< FlagField_T >(blocks, flagFieldID, wallFlagUID, fluidFlagUID);

      WALBERLA_LOG_INFO_ON_ROOT("Initialisation of the normals-field done")

      ////////////////
      // TIME LOOP //
      //////////////
      SweepTimeloop timeloop(blocks->getBlockStorage(), timesteps);

      timeloop.add() << BeforeFunction(Comm_velocity_based_distributions_start, "Start Hydro PDFs Communication")
                     << Sweep(phase_field_LB_NoSlip, "NoSlip Phase");
      timeloop.add() << Sweep(phase_field_LB_step, "Phase LB Step")
                     << AfterFunction(Comm_velocity_based_distributions_wait, "Wait Hydro PDFs Communication");
      timeloop.add() << Sweep(contact_angle, "Contact Angle")
                     << AfterFunction(Comm_phase_field, "Communication Phase Field");

      timeloop.add() << BeforeFunction(Comm_phase_field_distributions_start, "Start Phase PDFs Communication")
                     << Sweep(hydro_LB_NoSlip, "NoSlip Hydro");
      timeloop.add() << Sweep(hydro_LB_step, "Hydro LB Step")
                     << AfterFunction(Comm_phase_field_distributions_wait, "Wait Phase PDFs Communication");

      if (scenario == 4)
      {
         python_coupling::PythonCallback smear_interface("interface_diffusion");
         if (smear_interface.isCallable())
         {
            smear_interface.data().exposeValue("blocks", blocks);
            smear_interface();
         }
      }
      gpu::fieldCpy< GPUField, PhaseField_T >(blocks, phase_field_gpu, phase_field);
      WALBERLA_GPU_CHECK(cudaPeekAtLastError())

      WALBERLA_LOG_INFO_ON_ROOT("Initialisation of the PDFs")
      for (auto& block : *blocks)
      {
         init_h(&block);
         init_g(&block);
      }
      Comm_phase_field_distributions();

      WALBERLA_LOG_INFO_ON_ROOT("Initialisation of the PDFs done")
      uint_t dbWriteFrequency = parameters.getParameter< uint_t >("dbWriteFrequency", 0);
      int targetRank          = 0;

      if (dbWriteFrequency > 0)
      {
         timeloop.addFuncAfterTimeStep(
            [&]() {
               if (timeloop.getCurrentTimeStep() % dbWriteFrequency == 0)
               {
                  gpu::fieldCpy< PhaseField_T, GPUField >(blocks, phase_field, phase_field_gpu);
                  gpu::fieldCpy< VelocityField_T, GPUField >(blocks, vel_field, vel_field_gpu);
                  WALBERLA_GPU_CHECK(cudaPeekAtLastError())

                  if (scenario == 4)
                  {
                     std::array< real_t, 4 > total_velocity = { 0.0, 0.0, 0.0, 0.0 };
                     real_t volume;
                     uint_t nrCells;
                     PhaseField_T gatheredPhaseField(0, 0, 0, 0);
                     VelocityField_T gatheredVelocityField(0, 0, 0, 0);

                     CellInterval boundingBox = blocks->getDomainCellBB();
                     if (cell_idx_t(center_of_mass[1] - cell_idx_t(cellsPerBlock[0]) * 1.5) >= 0)
                        boundingBox.min()[1] = cell_idx_t(center_of_mass[1] - cell_idx_t(cellsPerBlock[0]) * 1.5);
                     if (cell_idx_t(center_of_mass[1] + cell_idx_t(cellsPerBlock[0]) * 1.5) <= boundingBox.max()[1])
                        boundingBox.max()[1] = cell_idx_t(center_of_mass[1] + cell_idx_t(cellsPerBlock[0]) * 1.5);

                     field::gather< PhaseField_T >(gatheredPhaseField, blocks, phase_field, boundingBox, targetRank);
                     field::gather< VelocityField_T >(gatheredVelocityField, blocks, vel_field, boundingBox,
                                                      targetRank);

                     WALBERLA_EXCLUSIVE_WORLD_SECTION(targetRank)
                     {
                        flood_fill(gatheredPhaseField, gatheredVelocityField, boundingBox, volume, nrCells,
                                   center_of_mass, total_velocity);
                     }
                     WALBERLA_MPI_SECTION() { walberla::mpi::broadcastObject(center_of_mass, targetRank); }

                     python_coupling::PythonCallback callback("at_end_of_time_step");
                     if (callback.isCallable())
                     {
                        callback.data().exposeValue("blocks", blocks);
                        callback.data().exposeValue("timeStep", timeloop.getCurrentTimeStep());
                        callback.data().exposeValue("target_rank", targetRank);
                        callback.data().exposeValue("bounding_box_min", boundingBox.min()[1]);
                        callback.data().exposeValue("bounding_box_max", boundingBox.max()[1]);
                        callback.data().exposeValue("total_velocity", total_velocity[0]);
                        callback.data().exposeValue("total_velocity_X", total_velocity[1]);
                        callback.data().exposeValue("total_velocity_Y", total_velocity[2]);
                        callback.data().exposeValue("total_velocity_Z", total_velocity[3]);
                        callback.data().exposeValue("center_of_mass_X", center_of_mass[0]);
                        callback.data().exposeValue("center_of_mass_Y", center_of_mass[1]);
                        callback.data().exposeValue("center_of_mass_Z", center_of_mass[2]);
                        callback.data().exposeValue("sum_inv_phi", volume);
                        callback.data().exposeValue("gas_cells_of_the_taylor_bubble", nrCells);
                        callback.data().exposeValue("stencil_phase", StencilNamePhase);
                        callback.data().exposeValue("stencil_hydro", StencilNameHydro);
                        callback();
                     }
                  }
                  else
                  {
                     python_coupling::PythonCallback callback("at_end_of_time_step");
                     if (callback.isCallable())
                     {
                        callback.data().exposeValue("blocks", blocks);
                        callback.data().exposeValue("timeStep", timeloop.getCurrentTimeStep());
                        callback.data().exposeValue("stencil_phase", StencilNamePhase);
                        callback.data().exposeValue("stencil_hydro", StencilNameHydro);
                        callback();
                     }
                  }
               }
            },
            "Python callback");
      }

      int meshWriteFrequency = parameters.getParameter< int >("meshWriteFrequency", 0);
      int counter            = 0;
      if (meshWriteFrequency > 0)
      {
         timeloop.addFuncAfterTimeStep(
            [&]() {
               if (timeloop.getCurrentTimeStep() % uint_t(meshWriteFrequency) == 0)
               {
                  auto mesh = postprocessing::realFieldToSurfaceMesh< PhaseField_T >(blocks, phase_field, 0.5, 0, true,
                                                                                     targetRank, MPI_COMM_WORLD);
                  WALBERLA_EXCLUSIVE_WORLD_SECTION(targetRank)
                  {
                     const std::string path = "";
                     std::ostringstream out;
                     out << std::internal << std::setfill('0') << std::setw(6) << counter;
                     geometry::writeMesh(
                        path + "taylor_bubble_D_" + std::to_string(cellsPerBlock[0]) + "_" + out.str() + ".obj", *mesh);
                     counter++;
                  }
               }
            },
            "Mesh writer");
      }

      // remaining time logger
      timeloop.addFuncAfterTimeStep(
         timing::RemainingTimeLogger(timeloop.getNrOfTimeSteps(), remainingTimeLoggerFrequency),
         "remaining time logger");

      uint_t const vtkWriteFrequency = parameters.getParameter< uint_t >("vtkWriteFrequency", 0);
      if (vtkWriteFrequency > 0)
      {
         const std::string path = "vtk_out";
         auto vtkOutput         = vtk::createVTKOutput_BlockData(*blocks, "vtk", vtkWriteFrequency, 0, false, path,
                                                         "simulation_step", false, true, true, false, 0);
         vtkOutput->addBeforeFunction([&]() {
            gpu::fieldCpy< PhaseField_T, GPUField >(blocks, phase_field, phase_field_gpu);
            gpu::fieldCpy< VelocityField_T, GPUField >(blocks, vel_field, vel_field_gpu);
         });
         WALBERLA_GPU_CHECK(cudaPeekAtLastError())

         auto phaseWriter = make_shared< field::VTKWriter< PhaseField_T, float > >(phase_field, "PhaseField");
         vtkOutput->addCellDataWriter(phaseWriter);

         auto flagWriter = make_shared< field::VTKWriter< FlagField_T > >(flagFieldID, "flag");
         vtkOutput->addCellDataWriter(flagWriter);

         auto velWriter = make_shared< field::VTKWriter< VelocityField_T, float > >(vel_field, "Velocity");
         vtkOutput->addCellDataWriter(velWriter);

         timeloop.addFuncBeforeTimeStep(vtk::writeFiles(vtkOutput), "VTK Output");
      }

      lbm::PerformanceEvaluation< FlagField_T > const performance(blocks, flagFieldID, fluidFlagUID);
      WcTimingPool timeloopTiming;
      WcTimer simTimer;

      WALBERLA_MPI_WORLD_BARRIER()
      cudaDeviceSynchronize();
      WALBERLA_GPU_CHECK(cudaPeekAtLastError())

      WALBERLA_LOG_INFO_ON_ROOT("Starting simulation with " << timesteps << " time steps")
      simTimer.start();
      timeloop.run(timeloopTiming);

      cudaDeviceSynchronize();
      WALBERLA_GPU_CHECK(cudaPeekAtLastError())

      simTimer.end();
      auto time = real_c(simTimer.max());
      WALBERLA_MPI_SECTION() { walberla::mpi::reduceInplace(time, walberla::mpi::MAX); }
      performance.logResultOnRoot(timesteps, time);

      const auto reducedTimeloopTiming = timeloopTiming.getReduced();
      WALBERLA_LOG_RESULT_ON_ROOT("Time loop timing:\n" << *reducedTimeloopTiming)
   }
   return EXIT_SUCCESS;
}
