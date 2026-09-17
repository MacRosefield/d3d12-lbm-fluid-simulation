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

  gims::f32v3 getCameraPosition();

  void updateUiDataStruct();

  void createDepthStencilSRV(const ComPtr<ID3D12Resource>& depthTexture);

  void createSRVandUAV();

  void createComputeRootSignature();
  void createComputePipeline();

  void CreateLBMBuffers();
  void InitializeLBMGridWithStreifen();

  void createLBMComputeRootSignature();
  void createLBMPipeline();

  //void InitializeCelltypeCondition();

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

  ComPtr<ID3D12PipelineState> m_computePipelineStateLBM;
  ComPtr<ID3D12PipelineState> m_computePipelineStateLBMStream;

  ComPtr<ID3D12DescriptorHeap> m_depthSrvDescriptorHeap;
  ComPtr<ID3D12DescriptorHeap> m_postSrvDescriptorHeap;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12RootSignature> m_rootSignatureDepth;
  ComPtr<ID3D12RootSignature> m_rootSignaturePT;
  ComPtr<ID3D12RootSignature> m_rootSignaturePP;
  ComPtr<ID3D12RootSignature> m_computeRootSignature;

  ComPtr<ID3D12RootSignature> m_LBMcomputeRootSignature;

  std::vector<ConstantBufferD3D12> m_constantBuffers;
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

  ComPtr<ID3D12Resource> m_GridBufferA;
  ComPtr<ID3D12Resource> m_GridBufferB;
  bool                   mUseBufferA = true;
  ComPtr<ID3D12Resource> m_CellTypeBuffer;

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
