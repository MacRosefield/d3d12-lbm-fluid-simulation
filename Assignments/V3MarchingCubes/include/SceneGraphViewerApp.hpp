// SceneGraphViewerApp.hpp
#ifndef SCENE_GRAPH_VIEWER_APP_CLASS
#define SCENE_GRAPH_VIEWER_APP_CLASS

#include "LightStruct.h"
#include "Scene.hpp"
#include "UiDataStruct.h"
#include <gimslib/d3d/DX12App.hpp>
#include <gimslib/types.hpp>
#include <gimslib/ui/ExaminerController.hpp>

/// <summary>
/// An app for viewing an Asset Importer Scene Graph.
/// </summary>
class SceneGraphViewerApp : public gims::DX12App
{
public:
  /// <summary>
  /// Creates the SceneGraphViewerApp and loads a scene.
  /// </summary>
  /// <param name="config">Configuration.</param>
  SceneGraphViewerApp(const gims::DX12AppConfig config, const std::filesystem::path pathToScene);

  ~SceneGraphViewerApp() = default;

  /// <summary>
  /// Called whenever a new frame is drawn.
  /// </summary>
  virtual void onDraw();

  /// <summary>
  /// Called whenever window size is changed.
  /// </summary>
  virtual void onResize();

  /// <summary>
  /// Draw UI onto of rendered result.
  /// </summary>
  virtual void onDrawUI();

private:
  /// <summary>
  /// Root signature connecting shader and GPU resources.
  /// </summary>
  void createGraphicRootSignature();

  /// <summary>
  /// Creates the pipeline
  /// </summary>
  void createGraphicPipeline();

  /// <summary>
  /// Draws the scene.
  /// </summary>
  /// <param name="commandList">Command list to which we upload the buffer</param>
  void drawScene(const ComPtr<ID3D12GraphicsCommandList>& commandList);

  void createSceneConstantBuffer();
  void updateSceneConstantBuffer();

  void createLBMconstantBuffer();
  void updateLBMconstantBuffer();

  gims::f32v3 getCameraPosition();

  void updateUiDataStruct();

  void createDepthStencilSRV(const ComPtr<ID3D12Resource>& depthTexture);

  void createSRVandUAV();

  void createComputeRootSignature();
  void createDebugQuadRootSignature();
  void createComputePipeline();

  void CreateLBMBuffers();
  void CreateLBM3DBuffers();
  void InitializeLBMGridWithStreifen();
  void InitializeLBMGrid3D();
  void InitializeLBMGrid3D_v2();

  void createLBMComputeRootSignature();
  void createLBM3DComputeRootSignature();
  void createLBMPipeline();
  void createLBM3DPipeline();

  void createMeshRootSignature();
  void createMeshPipeline();
  void createDebugMeshPipeline();
  void createDebugQuadPipeline();

  void checkMeshShaderSupport();

  void createMarchingCubesLookupBuffers();
  void uploadMarchingCubesLookupTables();

  void createDebugQuad();
  void createDebugQuadBuffers(UINT vertexBufferSize, UINT indexBufferSize);

  void createTriangleCounterResources();

  // == = GPU Timing == = private :
  // Wie viele Frames parallel? (an deine FrameResources anpassen!)
  static inline constexpr UINT kFramesInFlight = 3;

  // Obergrenze an Stamps pro Frame (sicher großzügig wählen)
  static inline constexpr UINT kStampsMaxPerFrame = 128;

  // Messpunkte (du kannst die Liste jederzeit erweitern)
  enum class GpuMark : uint32_t
  {
    Frame_Begin,
    Scene_Draw_End,
    Offscreen_To_UAV_End,

    PostFog_Begin,
    PostFog_Mesh_End,
    PostFog_Debug_End,

    LBM2D_Begin,
    LBM2D_Collision_End,
    LBM2D_Stream_End,
    LBM2D_CopyToBackbuffer_End,

    LBM3D_Begin,
    LBM3D_Collision_End,
    LBM3D_Stream_End,
    LBM3D_UpdateCellTypes_End,
    LBM3D_CopyToBackbuffer_End,

    LBMMC_Begin,
    LBMMC_Dispatch_End,

    DebugPlane_Begin,
    DebugPlane_Draw_End,

    Frame_End,
    Count
  };

  // Ressourcen
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsHeap;              // Timestamp-Query-Heap
  Microsoft::WRL::ComPtr<ID3D12Resource>  m_tsReadback;          // READBACK-Buffer (alle Frames × kStampsMaxPerFrame)
  UINT64                                  m_tsFrequency   = 0;   // Ticks/s der Queue
  double                                  m_timestampToMs = 0.0; // ms pro Tick

  // Cursor pro Frame: wie viele Stamps wurden wirklich geschrieben?
  UINT m_tsCursor[kFramesInFlight] = {};

  // Mapping: für jeden Mark -> absoluter Index im Readback-Buffer (oder UINT_MAX, wenn in diesem Frame nicht
  // gestempelt)
  UINT m_markToIndex[kFramesInFlight][static_cast<UINT>(GpuMark::Count)];

  // Ergebnis-Puffer (optional – praktisch fürs ImGui)
  double m_gpuMs[static_cast<UINT>(GpuMark::Count)] = {};
  double m_mlups                                    = std::numeric_limits<double>::quiet_NaN();
  // VRAM counter
  double m_vramBudgetGiB = std::numeric_limits<double>::quiet_NaN();
  double m_vramUsageGiB  = std::numeric_limits<double>::quiet_NaN();

  // Helpers
  inline UINT stampBase(UINT frameIdx) const
  {
    return frameIdx * kStampsMaxPerFrame;
  }

  // Start/Ende und Stempel
  void        createGpuTimingResources();
  inline void BeginGpuTiming(UINT frameIdx);
  inline void Stamp(ID3D12GraphicsCommandList* cl, UINT frameIdx, GpuMark mark);
  inline void EndGpuTiming(ID3D12GraphicsCommandList* cl, UINT frameIdx);

  // Auslesen im nächsten Frame
  void collectGpuTimes();
  void queryVram();
  void collectMeshStats();


  // void InitializeCelltypeCondition();

  ComPtr<ID3D12PipelineState> m_pipelineState;
  ComPtr<ID3D12PipelineState> m_pipelineStateBB;
  ComPtr<ID3D12PipelineState> m_pipelineStateDepthTexture;
  ComPtr<ID3D12PipelineState> m_pipelineStatePT;
  ComPtr<ID3D12PipelineState> m_pipelineStatePostDOF;
  ComPtr<ID3D12PipelineState> m_pipelineStatePostVignet;
  ComPtr<ID3D12PipelineState> m_pipelineStatePostLut;
  ComPtr<ID3D12PipelineState> m_computePipelineStateVignet;
  ComPtr<ID3D12PipelineState> m_computePipelineStateSepia;
  ComPtr<ID3D12PipelineState> m_computePipelineStateGrain;

  // ComPtr<ID3D12PipelineState> m_computePipelineState;
  ComPtr<ID3D12PipelineState> m_computePipelineStateHorizontal;
  ComPtr<ID3D12PipelineState> m_computePipelineStateVertical;
  ComPtr<ID3D12PipelineState> m_computePipelineStateDoF;
  ComPtr<ID3D12PipelineState> m_computePipelineStateFog;

  ComPtr<ID3D12PipelineState> m_debugPlanepipelineStateCelltype;
  ComPtr<ID3D12PipelineState> m_debugPlanepipelineStateRho;
  ComPtr<ID3D12PipelineState> m_debugPlanepipelineStateM;
  ComPtr<ID3D12PipelineState> m_debugPlanepipelineStateEpsilon;

  ComPtr<ID3D12PipelineState> m_computePipelineStateLBM;
  ComPtr<ID3D12PipelineState> m_computePipelineStateLBMStream;
  ComPtr<ID3D12PipelineState> m_computePipelineStateLBM3D;
  ComPtr<ID3D12PipelineState> m_computePipelineStateLBM3DStream;
  ComPtr<ID3D12PipelineState> m_computePipelineStateLBM3DUpdateCellTypes;
  ComPtr<ID3D12PipelineState> m_meshPipelineState;
  ComPtr<ID3D12PipelineState> m_meshDebugPipelineState;

  ComPtr<ID3D12DescriptorHeap> m_depthSrvDescriptorHeap;
  ComPtr<ID3D12DescriptorHeap> m_postSrvDescriptorHeap;

  ComPtr<ID3D12RootSignature> m_rootSignature;

  ComPtr<ID3D12RootSignature> m_rootSignatureDepth;
  ComPtr<ID3D12RootSignature> m_rootSignaturePT;
  ComPtr<ID3D12RootSignature> m_rootSignaturePP;
  ComPtr<ID3D12RootSignature> m_DebugQuadRootSig;
  ComPtr<ID3D12RootSignature> m_computeRootSignature;

  ComPtr<ID3D12RootSignature> m_LBMcomputeRootSignature;
  ComPtr<ID3D12RootSignature> m_LBM3DcomputeRootSignature;
  ComPtr<ID3D12RootSignature> m_meshRootSignature;

  std::vector<ConstantBufferD3D12> m_constantBuffers;
  std::vector<ConstantBufferD3D12> m_constantBuffersLBM;
  gims::ExaminerController         m_examinerController;
  Scene                            m_scene;
  UiData                           m_uiData;
  Light                            m_Lights[8];

  // #MH Offscreen Render Target
  ComPtr<ID3D12Resource> m_offscreenRenderTarget;
  ComPtr<ID3D12Resource> m_offscreenDepthStencil;
  ComPtr<ID3D12Resource> m_computeOutputTexture;
  ComPtr<ID3D12Resource> m_pingPongBuffer;

  ComPtr<ID3D12Resource> m_offscreenTarget;
  ComPtr<ID3D12Resource> m_gaussianTarget_x;
  ComPtr<ID3D12Resource> m_gaussianTarget_y;
  ComPtr<ID3D12Resource> m_finalTarget;

  ComPtr<ID3D12Resource> m_depthTexture;

  ComPtr<ID3D12Resource>   m_DebugVB;
  ComPtr<ID3D12Resource>   m_DebugIB;
  D3D12_VERTEX_BUFFER_VIEW m_DebugVBView;
  D3D12_INDEX_BUFFER_VIEW  m_DebugIBView;
  UINT                     m_DebugIndexCount = 0;

  ComPtr<ID3D12Resource> m_GridBufferA;
  ComPtr<ID3D12Resource> m_GridBufferB;
  ComPtr<ID3D12Resource> m_GridBufferA3D;
  ComPtr<ID3D12Resource> m_GridBufferB3D;
  bool                   mUseBufferA  = true;
  int                    frameCounter = 0;
  ComPtr<ID3D12Resource> m_CellTypeBuffer;
  ComPtr<ID3D12Resource> m_CellTypeBuffer3D;

  // #MH LookUp table buffers
  ComPtr<ID3D12Resource> m_EdgeTableBuffer; // uint edgeTable[256]
  ComPtr<ID3D12Resource> m_TriTableBuffer;  // int triTable[256 * 16]


  // GPU triangle counter
  ComPtr<ID3D12Resource> m_triCountUAV;      // DEFAULT, 4 bytes (uint)
  ComPtr<ID3D12Resource> m_triCountReadback; // READBACK, 4 bytes
  ComPtr<ID3D12Resource> m_triZeroUpload;    // UPLOAD, 4 bytes with zero

  uint32_t             m_triCountFrame        = 0;
  static constexpr int kTriHistory            = 240;
  float                m_triHist[kTriHistory] = {};
  int                  m_triHistIdx           = 0;
  double               m_triAvg               = std::numeric_limits<double>::quiet_NaN();
  double               m_triMax               = 0.0;
  static constexpr UINT kFrameCount            = 3; 
  const UINT64          rbSize                 = UINT64(kFrameCount) * sizeof(UINT32);

  CD3DX12_CPU_DESCRIPTOR_HANDLE m_triCountUAV_CPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_triCountUAV_GPU_UAV;


  CD3DX12_CPU_DESCRIPTOR_HANDLE m_offscreenTarget_CPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_offscreenTarget_GPU_UAV;
  CD3DX12_CPU_DESCRIPTOR_HANDLE m_offscreenTarget_CPU_RTV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_offscreenTarget_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_depthTexture_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gaussianBlur_X_GPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gaussianBlur_X_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gaussianBlur_Y_GPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gaussianBlur_Y_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_pingPongBuffer_GPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_pingPongBuffer_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_finalTarget_GPU_UAV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_finalTarget_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_edgeTable_GPU_SRV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_triTable_GPU_SRV;

  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gridBufferA_GPU_SRV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_gridBufferA3D_GPU_SRV;
  CD3DX12_GPU_DESCRIPTOR_HANDLE m_cellTypeBufferA3D_GPU_SRV;

  ComPtr<ID3D12DescriptorHeap>  m_offscreenRtvHeap;
  CD3DX12_CPU_DESCRIPTOR_HANDLE m_offscreenRtvHandle;

  ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
  ComPtr<ID3D12DescriptorHeap> m_rtvDescriptorHeap;

  // #MH Offscreen Depth Stencil Buffer

  ComPtr<ID3D12DescriptorHeap>  m_offscreenDsvHeap;
  CD3DX12_CPU_DESCRIPTOR_HANDLE m_offscreenDsvHandle;

  // #MH Compute UAV texture

  ComPtr<ID3D12DescriptorHeap> m_offscreenUavHeap;

  FullScreenQuad m_fullscreenQuad;
};
#endif // SCENE_GRAPH_VIEWER_APP_CLASS
