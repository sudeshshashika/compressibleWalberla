###################################################################################################
#
# Module stencil
#
###################################################################################################

add_library( walberla_stencil INTERFACE )
add_library( walberla::stencil ALIAS walberla_stencil )
target_sources( walberla_stencil
      INTERFACE
      AABBQuadrant.h
      D2CornerStencil.h
      D2Q4.h
      D2Q5.h
      D2Q9.h
      D3CornerStencil.h
      D3EdgeCornerStencil.h
      D3Q15.h
      D3Q19.h
      D3Q27.h
      D3Q6.h
      D3Q7.h
      Directions.h
      EdgeStencil.h
      Iterator.h
      Stencil.in.h
      )
target_link_libraries( walberla_stencil INTERFACE walberla::core )

###################################################################################################
