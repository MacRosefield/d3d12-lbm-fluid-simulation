// VertexStruct.h
#ifndef LBMCELL_STRUCT
#define LBMCELL_STRUCT

#include <gimslib/types.hpp>

struct LBCell
{
  gims::f32   rho;
  gims::f32v2 u;
  gims::f32   f[9];
};

#endif // LBMCELL_STRUCT
