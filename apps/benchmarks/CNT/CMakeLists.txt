waLBerla_link_files_to_builddir( *.dat )

waLBerla_add_executable( NAME 01_cnt_film
   FILES 01_cnt_film.cpp
        Config.cpp
        FilmSpecimen.cpp
        InitializeCNTs.cpp
        SQLProperties.cpp
        Statistics.cpp
      DEPENDS walberla::blockforest walberla::core walberla::mesa_pd walberla::sqlite walberla::vtk )
