
waLBerla_link_files_to_builddir( "*.dat" )   

waLBerla_add_executable( NAME SchaeferTurek DEPENDS walberla::blockforest walberla::boundary walberla::core walberla::field walberla::lbm walberla::postprocessing walberla::stencil walberla::timeloop walberla::vtk walberla::sqlite )

waLBerla_execute_test( NO_MODULE_LABEL NAME SchaeferTurekTest COMMAND $<TARGET_FILE:SchaeferTurek> Test2D.dat PROCESSES 4 CONFIGURATIONS Release RelWithDbgInfo DEPENDS_ON_TARGETS SchaeferTurek )