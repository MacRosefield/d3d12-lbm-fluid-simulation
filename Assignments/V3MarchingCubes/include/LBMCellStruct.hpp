// VertexStruct.h
#ifndef LBMCELL_STRUCT
#define LBMCELL_STRUCT

#include <gimslib/types.hpp>

// CELL for 2D LBM solving
struct LBCell
{
  gims::f32   rho;
  gims::f32v2 u;
  gims::f32   f[9];
};

// CELL for 3D LBM solving
struct LBCell3D
{
  gims::f32   rho;
  gims::f32v3 u;
  gims::f32   f[19];
  gims::f32   m;
  gims::f32   epsilon;
};

#endif // LBMCELL_STRUCT
