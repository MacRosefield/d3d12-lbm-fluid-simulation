// ConstantBufferStruct.h
#ifndef CONSTANT_BUFFER_STRUCT
#define CONSTANT_BUFFER_STRUCT

#include "LightStruct.h"
#include <gimslib/types.hpp>

struct ConstantBuffer
{
  gims::f32m4 projectionMatrix;
  gims::f32v3 cameraPosition;
  gims::ui32  numOfLights;
  Light       Lights[8];
  gims::f32v3 boundingBoxColor;

  // #MH - DOF values
  gims::f32  focalDistance;
  gims::f32  blurStrength;
  gims::ui32 blurRadius;
  gims::f32  nearPlane;
  gims::f32  farPlane;
  gims::f32  vigValue;
  gims::f32  grainValue;

  // #MH LBM values
  gims::f32 colorIntens;
  gims::f32 inflow_u;
  gims::f32 outflow_u;
};
#endif // CONSTANT_BUFFER_STRUCT
