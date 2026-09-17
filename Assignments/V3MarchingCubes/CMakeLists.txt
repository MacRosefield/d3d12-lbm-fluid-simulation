include("../../CreateApp.cmake")
set(SOURCES "./src/main.cpp" 
                                "./src/SceneGraphViewerApp.cpp" 
								"./src/AABB.cpp" 
								"./src/Scene.cpp" 
								"./src/SceneFactory.cpp" 
								"./src/TriangleMeshD3D12.cpp" 
								"./src/Texture2DD3D12.cpp" 
								"./src/ConstantBufferD3D12.cpp" 
								"./src/BoundingBox.cpp"
								"./src/FullScreenQuad.cpp"
								"./include/LookupTables.h"
								"./src/LookupTables.cpp"
"./src/DebugQuad.cpp"
								"./include/AABB.hpp" 
								"./include/Scene.hpp" 
								"./include/SceneFactory.hpp" 
								"./include/TriangleMeshD3D12.hpp" 								
								"./include/Texture2DD3D12.hpp" 								
								"./include/SceneGraphViewerApp.hpp"
								"./include/ConstantBufferD3D12.hpp"
								"./include/VertexStruct.h"
								"./include/NodeStruct.h"
								"./include/MaterialConstantBufferStruct.h"
								"./include/MaterialStruct.h"
								"./include/ConstantBufferStruct.h"
								"./include/PerMeshConstantBufferStruct.h"
								"./include/LightStruct.h"
								"./include/UiDataStruct.h"
								"./include/BoundingBox.h"
								"./include/FullScreenQuad.h"
								"./include/Drawable.h"
								"./include/LBMCellStruct.hpp"
								"./include/LBMCelltypes.h"
								"./include/MeshPipelineTypes.hpp"
"./include/DebugQuad.hpp")


set(SHADERS "./shaders/TriangleMesh.hlsl" "./shaders/BoundingBox.hlsl" 
"./shaders/ComputeShader.hlsl" 
"./shaders/FullScreenQuad.hlsl" "./shaders/DepthBuffer.hlsl" "./shaders/PostProcess.hlsl"  "./shaders/PassThrough.hlsl"   "./shaders/DepthOfField.hlsl" "./shaders/LBM.hlsl" "./shaders/LBMStreaming.hlsl" "./shaders/MarchingLBM.hlsl" "./shaders/LBM3D.hlsl" "./shaders/LBM3DStreaming.hlsl" "./shaders/LBM3D_UpdateCellTypes.hlsl" "./shaders/LBM3DCommon.hlsl" "./shaders/VSDebugLBM3D.hlsl" "./shaders/PSDebugLBM3D.hlsl")
create_app(V3MarchingCubes "${SOURCES}" "${SHADERS}")
find_package(assimp CONFIG REQUIRED)

target_link_libraries(V3MarchingCubes PRIVATE assimp::assimp)
