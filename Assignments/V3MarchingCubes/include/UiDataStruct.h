// UiDataStruct.h
#ifndef USER_INTERFACE_DATA_STRUCT
#define USER_INTERFACE_DATA_STRUCT

#include <gimslib/types.hpp>

struct UiData
{
  gims::f32v3 backgroundColor            = gims::f32v3(0.0f, 0.0f, 0.0f);
  gims::ui32  numberOfNodes              = gims::ui32(0);
  gims::ui32  numberOfMeshes             = gims::ui32(0);
  gims::ui32  numberOfMaterials          = gims::ui32(0);
  gims::ui32  numberOfTextures           = gims::ui32(0);
  gims::f32v3 sceneLowerleftAABBPosition = gims::f32v3(0.0f, 0.0f, 0.0f);
  gims::f32v3 sceneTopRightAABBPosition  = gims::f32v3(0.0f, 0.0f, 0.0f);
  gims::f32v3 boundingBoxColor           = gims::f32v3(1.0f, 1.0f, 1.0f);
  gims::i32   numOfLightsInScene         = gims::ui32(1);
  gims::f32   nearPlane                  = gims::f32(0.1f);
  gims::f32   farPlane                   = gims::f32(500.0f);
  gims::ui32  debugPlanePos              = gims::ui32(0);

  bool displayDepthTexture  = false;
  bool displayPostDOF       = false;
  bool displayPostVignet    = false;
  bool displayPostFog       = false;
  bool displayFluid         = false;
  bool displayLBMMC         = true;
  bool displayPostGrain     = false;
  bool displayBoundingBoxes = false;
  bool displaySceneData     = true;
  bool displayPostSepia     = false;

  // LBM 2D
  bool displayLBM2D = false;

  // DEBUG LBM 3D
  bool      displayDebugPlane    = false;
  gims::i32 zSlicePosition       = gims::ui32(1);
  gims::i32 debugSimSpeed        = gims::ui32(1);
  bool      displayDebugCelltype = false;
  bool      displayDebugRho      = false;
  bool      displayDebugM        = false;
  bool      displayDebugEpsilon  = false;

  gims::f32 focalDistance = 1.0f; // distance to the point in focus
  gims::f32 blurStrength  = 1.0f; // strenght of the blur effect
  gims::i32 blurRadius    = 2;    // radius in pixel of the blur mask
  gims::f32 vignetIntes   = 1.0f; // vignet intensity
  gims::f32 grainIntes    = 0.4f; // grain intensity
  gims::f32 colorValue    = 1.0f;
  gims::f32 inflowSpeed   = 1.0f;
  gims::f32 outflowSpeed  = 1.0f;
  gims::f32 lbmTau        = 4.0f;

  gims::i32 gridAdjusterX = 328;
  gims::i32 gridAdjusterY = 128;
  gims::i32 gridAdjusterZ = 94;
};
#endif // USER_INTERFACE_DATA_STRUCT
