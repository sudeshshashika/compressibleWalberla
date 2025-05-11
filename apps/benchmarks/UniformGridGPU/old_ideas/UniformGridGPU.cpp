#include "core/Environment.h"
#include "core/logging/Initialization.h"
#include "core/math/Random.h"
#include "python_coupling/CreateConfig.h"
#include "python_coupling/PythonCallback.h"
#include "python_coupling/DictWrapper.h"
#include "blockforest/Initialization.h"
#include "field/FlagField.h"
#include "field/AddToStorage.h"
#include "field/vtk/VTKWriter.h"
#include "field/communication/PackInfo.h"
#include "lbm/PerformanceLogger.h"
#include "blockforest/communication/UniformBufferedScheme.h"
#include "timeloop/all.h"
#include "core/math/Random.h"
#include "geometry/all.h"
#include "gpu/HostFieldAllocator.h"
#include "gpu/communication/GPUPackInfo.h"
#include "gpu/ParallelStreams.h"
#include "gpu/NVTX.h"
#include "core/timing/TimingPool.h"
#include "core/timing/RemainingTimeLogger.h"
#include "gpu/AddGPUFieldToStorage.h"
#include "gpu/communication/UniformGPUScheme.h"
#include "gpu/DeviceSelectMPI.h"
#include "domain_decomposition/SharedSweep.h"

#include "UniformGridGPU_LatticeModel.h"
#include "UniformGridGPU_LbKernel.h"
#include "UniformGridGPU_PackInfo.h"
#include "UniformGridGPU_UBB.h"
#include "UniformGridGPU_NoSlip.h"
#include "UniformGridGPU_Communication.h"
#include "UniformGridGPU_MacroSetter.h"
#include "UniformGridGPU_MacroGetter.h"
#include "UniformGridGPU_Defines.h"

#include "InitShearVelocity.h"


using namespace walberla;

using LatticeModel_T = lbm::UniformGridGPU_LatticeModel;

const auto Q = LatticeModel_T::Stencil::Q;


using Stencil_T = LatticeModel_T::Stencil;
using CommunicationStencil_T = LatticeModel_T::CommunicationStencil;
using PdfField_T = GhostLayerField<real_t, Q>;
using CommScheme_T = gpu::communication::UniformGPUScheme<CommunicationStencil_T>;
using VelocityField_T = GhostLayerField<real_t, 3>;
using flag_t = walberla::uint8_t;
using FlagField_T = FlagField<flag_t>;

int main( int argc, char **argv )
{
   mpi::Environment env( argc, argv );
   gpu::selectDeviceBasedOnMpiRank();

   for( auto cfg = python_coupling::configBegin( argc, argv ); cfg != python_coupling::configEnd(); ++cfg )
   {
      WALBERLA_MPI_WORLD_BARRIER();

      auto config = *cfg;
      logging::configureLogging( config );
      auto blocks = blockforest::createUniformBlockGridFromConfig( config );

      Vector3<uint_t> cellsPerBlock = config->getBlock( "DomainSetup" ).getParameter<Vector3<uint_t>  >( "cellsPerBlock" );
      // Reading parameters
      auto parameters = config->getOneBlock( "Parameters" );
      const std::string timeStepStrategy = parameters.getParameter<std::string>( "timeStepStrategy", "normal");
      const real_t omega = parameters.getParameter<real_t>( "omega", real_c( 1.4 ));
      const uint_t timesteps = parameters.getParameter<uint_t>( "timesteps", uint_c( 50 ));
      const bool initShearFlow = parameters.getParameter<bool>("initShearFlow", true);

      // Creating fields
      BlockDataID pdfFieldCpuID = field::addToStorage< PdfField_T >( blocks, "pdfs cpu", real_t(0), field::fzyx);
      BlockDataID velFieldCpuID = field::addToStorage< VelocityField_T >( blocks, "vel", real_t(0), field::fzyx);

      if( timeStepStrategy != "kernelOnlyNoInit")
      {
          if ( initShearFlow )
          {
              WALBERLA_LOG_INFO_ON_ROOT( "Initializing shear flow" );
              initShearVelocity( blocks, velFieldCpuID );
          }

          pystencils::UniformGridGPU_MacroSetter setterSweep(pdfFieldCpuID, velFieldCpuID);
          for( auto & block : *blocks )
              setterSweep( &block );

          // setter sweep only initializes interior of domain - for push schemes to work a first communication is required here
          blockforest::communication::UniformBufferedScheme<CommunicationStencil_T> initialComm(blocks);
          initialComm.addPackInfo( make_shared< field::communication::PackInfo<PdfField_T> >( pdfFieldCpuID ) );
          initialComm();
      }

      BlockDataID pdfFieldGpuID = gpu::addGPUFieldToStorage<PdfField_T >( blocks, pdfFieldCpuID, "pdfs on GPU", true );
      BlockDataID flagFieldID = field::addFlagFieldToStorage< FlagField_T >( blocks, "flag field" );


      // Boundaries
      const FlagUID fluidFlagUID( "Fluid" );
      auto boundariesConfig = config->getBlock( "Boundaries" );
      bool disableBoundaries = true;
      if( boundariesConfig )
      {
          disableBoundaries = false;
          geometry::initBoundaryHandling< FlagField_T >( *blocks, flagFieldID, boundariesConfig );
          geometry::setNonBoundaryCellsToDomain< FlagField_T >( *blocks, flagFieldID, fluidFlagUID );
      }

      lbm::UniformGridGPU_UBB ubb(blocks, pdfFieldGpuID);
      lbm::UniformGridGPU_NoSlip noSlip(blocks, pdfFieldGpuID);

      ubb.fillFromFlagField<FlagField_T>( blocks, flagFieldID, FlagUID("UBB"), fluidFlagUID );
      noSlip.fillFromFlagField<FlagField_T>( blocks, flagFieldID, FlagUID("NoSlip"), fluidFlagUID );

       // Communication setup
      bool cudaEnabledMPI = parameters.getParameter<bool>( "cudaEnabledMPI", false );
      Vector3<int32_t> gpuBlockSize = parameters.getParameter<Vector3<int32_t> > ("gpuBlockSize", Vector3<int32_t>(256, 1, 1));
      const std::string communicationSchemeStr = parameters.getParameter<std::string>("communicationScheme", "UniformGPUScheme_Baseline");
      CommunicationSchemeType communicationScheme;
      if( communicationSchemeStr == "GPUPackInfo_Baseline")
          communicationScheme = GPUPackInfo_Baseline;
      else if (communicationSchemeStr == "GPUPackInfo_Streams")
          communicationScheme = GPUPackInfo_Streams;
      else if (communicationSchemeStr == "UniformGPUScheme_Baseline")
          communicationScheme = UniformGPUScheme_Baseline;
      else if (communicationSchemeStr == "UniformGPUScheme_Memcpy")
          communicationScheme = UniformGPUScheme_Memcpy;
      else if (communicationSchemeStr == "MPIDatatypes")
          communicationScheme = MPIDatatypes;
      else if (communicationSchemeStr == "MPIDatatypesFull")
          communicationScheme = MPIDatatypesFull;
      else {
          WALBERLA_ABORT_NO_DEBUG_INFO("Invalid choice for communicationScheme")
      }

      Vector3<int> innerOuterSplit = parameters.getParameter<Vector3<int> >("innerOuterSplit", Vector3<int>(1, 1, 1));
      for(uint_t i=0; i< 3; ++i)
      {
          if( int_c(cellsPerBlock[i]) <= innerOuterSplit[i] * 2) {
              WALBERLA_ABORT_NO_DEBUG_INFO("innerOuterSplit too large - make it smaller or increase cellsPerBlock");
          }
      }

      int streamHighPriority = 0;
      int streamLowPriority = 0;
      WALBERLA_CUDA_CHECK( cudaDeviceGetStreamPriorityRange(&streamLowPriority, &streamHighPriority) );
      WALBERLA_CHECK(gpuBlockSize[2] == 1);
      pystencils::UniformGridGPU_LbKernel lbKernel( pdfFieldGpuID, omega,
                                                    1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7,
                                                    gpuBlockSize[0], gpuBlockSize[1],
                                                    Cell(innerOuterSplit[0], innerOuterSplit[1], innerOuterSplit[2]) );
      lbKernel.setOuterPriority( streamHighPriority );
      UniformGridGPU_Communication< CommunicationStencil_T, gpu::GPUField< double > >
         gpuComm( blocks, pdfFieldGpuID, (CommunicationSchemeType) communicationScheme, cudaEnabledMPI );

      auto defaultStream = gpu::StreamRAII::newPriorityStream( streamLowPriority );
      auto innerOuterStreams = gpu::ParallelStreams( streamHighPriority );
      auto boundaryOuterStreams = gpu::ParallelStreams( streamHighPriority );
      auto boundaryInnerStreams = gpu::ParallelStreams( streamHighPriority );

      uint_t currentTimeStep = 0;

      auto simpleOverlapTimeStep = [&] ()
      {
          gpuComm.startCommunication(defaultStream);
          for( auto &block: *blocks )
              lbKernel.inner( &block, defaultStream );
          gpuComm.wait(defaultStream);
          for( auto &block: *blocks )
              lbKernel.outer( &block, defaultStream );
      };

      auto overlapTimeStep = [&]()
      {
         gpu::NvtxRange namedRange("timestep");
         auto innerOuterSection = innerOuterStreams.parallelSection( defaultStream );

         innerOuterSection.run([&]( auto innerStream )
         {
            gpu::nameStream(innerStream, "inner stream");
            for( auto &block: *blocks )
            {
               if(!disableBoundaries)
               {
                  auto p = boundaryInnerStreams.parallelSection( innerStream );
                  p.run( [&block, &ubb]( cudaStream_t s ) { ubb.inner( &block, s ); } );
                  p.run( [&block, &noSlip]( cudaStream_t s ) { noSlip.inner( &block, s ); } );
               }
               lbKernel.inner( &block, innerStream );
            }
         });

         innerOuterSection.run([&]( auto outerStream )
         {
            gpu::nameStream(outerStream, "outer stream");
            gpuComm( outerStream );

            for( auto &block: *blocks )
            {
               if(!disableBoundaries)
               {
                  auto p = boundaryOuterStreams.parallelSection( outerStream );
                  p.run( [&block, &ubb]( cudaStream_t s ) { ubb.outer( &block, s ); } );
                  p.run( [&block, &noSlip]( cudaStream_t s ) { noSlip.outer( &block, s ); } );
               }
               lbKernel.outer( &block, outerStream );
            }
         });
         currentTimeStep += 1;
      };


      auto boundaryStreams = gpu::ParallelStreams( streamHighPriority );
      auto normalTimeStep = [&]()
      {
         gpuComm();
         for( auto &block: *blocks )
         {
            if(!disableBoundaries)
            {
               auto p = boundaryStreams.parallelSection( defaultStream );
               p.run( [&block, &ubb]( cudaStream_t s ) { ubb( &block, s ); } );
               p.run( [&block, &noSlip]( cudaStream_t s ) { noSlip( &block, s ); } );
            }
            lbKernel( &block );
         }
      };

      auto kernelOnlyFunc = [&] ()
      {
          for( auto &block: *blocks )
              lbKernel( &block );
      };

      SweepTimeloop timeLoop( blocks->getBlockStorage(), timesteps );

      std::function<void()> timeStep;
      if (timeStepStrategy == "noOverlap")
          timeStep = std::function<void()>( normalTimeStep );
      else if (timeStepStrategy == "complexOverlap")
          timeStep = std::function<void()>( overlapTimeStep );
      else if (timeStepStrategy == "simpleOverlap")
          timeStep = simpleOverlapTimeStep;
      else if (timeStepStrategy == "kernelOnly" or timeStepStrategy == "kernelOnlyNoInit") {
          WALBERLA_LOG_INFO_ON_ROOT("Running only compute kernel without boundary - this makes only sense for benchmarking!")
          timeStep = kernelOnlyFunc;
      }
      else {
          WALBERLA_ABORT_NO_DEBUG_INFO("Invalid value for 'timeStepStrategy'. Allowed values are 'noOverlap', 'complexOverlap', 'simpleOverlap', 'kernelOnly'");
      }

      timeLoop.add() << BeforeFunction( timeStep  )
                     << Sweep( []( IBlock * ) {}, "time step" );

      pystencils::UniformGridGPU_MacroGetter getterSweep( pdfFieldCpuID, velFieldCpuID );

      // VTK
      uint_t vtkWriteFrequency = parameters.getParameter<uint_t>( "vtkWriteFrequency", 0 );
      if( vtkWriteFrequency > 0 )
      {
         auto vtkOutput = vtk::createVTKOutput_BlockData( *blocks, "vtk", vtkWriteFrequency, 0, false, "vtk_out",
                                                          "simulation_step", false, true, true, false, 0 );
         auto velWriter = make_shared< field::VTKWriter<VelocityField_T> >(velFieldCpuID, "vel");
         vtkOutput->addCellDataWriter(velWriter);
         vtkOutput->addBeforeFunction( [&]() {
             gpu::fieldCpy<PdfField_T, gpu::GPUField<real_t> >( blocks, pdfFieldCpuID, pdfFieldGpuID );
             for( auto & block : *blocks )
                 getterSweep( &block );
         });
         timeLoop.addFuncAfterTimeStep( vtk::writeFiles( vtkOutput ), "VTK Output" );
      }



      int warmupSteps = parameters.getParameter<int>( "warmupSteps", 2 );
      int outerIterations = parameters.getParameter<int>( "outerIterations", 1 );
      for(int i=0; i < warmupSteps; ++i )
         timeLoop.singleStep();

      auto remainingTimeLoggerFrequency = parameters.getParameter< double >( "remainingTimeLoggerFrequency", -1.0 ); // in seconds
      if (remainingTimeLoggerFrequency > 0) {
          auto logger = timing::RemainingTimeLogger( timeLoop.getNrOfTimeSteps() * uint_c( outerIterations ), remainingTimeLoggerFrequency );
          timeLoop.addFuncAfterTimeStep( logger, "remaining time logger" );
      }

       for ( int outerIteration = 0; outerIteration < outerIterations; ++outerIteration )
       {
           timeLoop.setCurrentTimeStepToZero();
           WcTimer simTimer;
           cudaDeviceSynchronize();
           WALBERLA_LOG_INFO_ON_ROOT( "Starting simulation with " << timesteps << " time steps" );
           simTimer.start();
           timeLoop.run();
           cudaDeviceSynchronize();
           simTimer.end();
           WALBERLA_LOG_INFO_ON_ROOT( "Simulation finished" );
           auto time = simTimer.last();
           auto nrOfCells = real_c( cellsPerBlock[0] * cellsPerBlock[1] * cellsPerBlock[2] );
           auto mlupsPerProcess = nrOfCells * real_c( timesteps ) / time * 1e-6;
           WALBERLA_LOG_RESULT_ON_ROOT( "MLUPS per process " << mlupsPerProcess );
           WALBERLA_LOG_RESULT_ON_ROOT( "Time per time step " << time / real_c( timesteps ));
           WALBERLA_ROOT_SECTION()
           {
               python_coupling::PythonCallback pythonCallbackResults( "results_callback" );
               if ( pythonCallbackResults.isCallable())
               {
                   const char * storagePattern = "twofield";
                   pythonCallbackResults.data().exposeValue( "mlupsPerProcess", mlupsPerProcess );
                   pythonCallbackResults.data().exposeValue( "stencil", infoStencil );
                   pythonCallbackResults.data().exposeValue( "configName", infoConfigName );
                   pythonCallbackResults.data().exposeValue( "storagePattern", storagePattern );
                   pythonCallbackResults.data().exposeValue( "cse_global", infoCseGlobal );
                   pythonCallbackResults.data().exposeValue( "cse_pdfs", infoCsePdfs );
                   // Call Python function to report results
                   pythonCallbackResults();
               }
           }
       }

   }

   return 0;
}
