// UiDataStruct.h
#ifndef USER_INTERFACE_DATA_STRUCT
#define USER_INTERFACE_DATA_STRUCT

#include <gimslib/types.hpp>

struct UiData
{
  gims::f32v3 backgroundColor            = gims::f32v3(0.35f, 0.55f, 0.70f);
  gims::ui32  numberOfNodes              = gims::ui32(0);
  gims::ui32  numberOfMeshes             = gims::ui32(0);
  gims::ui32  numberOfMaterials          = gims::ui32(0);
  gims::ui32  numberOfTextures           = gims::ui32(0);
  gims::f32v3 sceneLowerleftAABBPosition = gims::f32v3(0.0f, 0.0f, 0.0f);
  gims::f32v3 sceneTopRightAABBPosition  = gims::f32v3(0.0f, 0.0f, 0.0f);
  gims::f32v3 boundingBoxColor           = gims::f32v3(1.0f, 1.0f, 1.0f);
  gims::i32   numOfLightsInScene         = gims::ui32(1);
  gims::f32   nearPlane                  = gims::f32(0.1f);
  gims::f32   farPlane                   = gims::f32(5.0f);
  bool        displayDepthTexture        = false;
  bool        displayPostDOF             = false;
  bool        displayPostVignet          = false;
  bool        displayPostFog             = false;
  bool        displayPostGrain           = false;
  bool        displayBoundingBoxes       = false;
  bool        displaySceneData           = true;
  bool        displayPostSepia           = false;

  gims::f32 focalDistance = 1.0f; // distance to the point in focus
  gims::f32 blurStrength  = 1.0f; // strenght of the blur effect
  gims::i32 blurRadius    = 2;    // radius in pixel of the blur mask
  gims::f32 vignetIntes   = 1.0f; // vignet intensity
  gims::f32 grainIntes    = 0.4f; // grain intensity
};
#endif // USER_INTERFACE_DATA_STRUCT
