// SceneGraphViewerApp.cpp

#include "SceneGraphViewerApp.hpp"
#include "ConstantBufferStruct.h"
#include "LBMCelltypes.h"
#include "LookupTables.h"
#include "MeshPipelineTypes.hpp"
#include "PerMeshConstantBufferStruct.h"
#include "SceneFactory.hpp"
#include <LBMCellStruct.hpp>
#include <d3dx12/d3dx12.h>
#include <gimslib/contrib/stb/stb_image.h>
#include <gimslib/d3d/DX12Util.hpp>
#include <gimslib/d3d/UploadHelper.hpp>
#include <gimslib/dbg/HrException.hpp>
#include <gimslib/io/CograBinaryMeshFile.hpp>
#include <gimslib/sys/Event.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>

SceneGraphViewerApp::SceneGraphViewerApp(const DX12AppConfig config, const std::filesystem::path pathToScene)
    : DX12App(config)
    , m_examinerController(true)
    , m_scene(SceneGraphFactory::createFromAssImpScene(pathToScene, getDevice(), getCommandQueue()))
{
  m_examinerController.setTranslationVector(gims::f32v3(0, -0.25f, 1.5));

  createGraphicRootSignature();
  createComputeRootSignature();
  createLBMComputeRootSignature();
  createLBM3DComputeRootSignature();

  // #MH compute Pipeline initialization
  createGraphicPipeline();
  createComputePipeline();
  createLBMPipeline();
  createLBM3DPipeline();

  // #MH debugging planes for lbm values
  createDebugQuadRootSignature();
  createDebugQuadPipeline();
  createDebugQuad();

  createSceneConstantBuffer();
  // createLBMconstantBuffer();

  createMarchingCubesLookupBuffers();
  uploadMarchingCubesLookupTables();

  createSRVandUAV();

  CreateLBMBuffers();
  InitializeLBMGridWithStreifen();

  CreateLBM3DBuffers();
  InitializeLBMGrid3D();
  // InitializeCelltypeCondition();

  // Meshshader impl
  createMeshRootSignature();
  createMeshPipeline();
  createDebugMeshPipeline();

  checkMeshShaderSupport();

  // === GPU Timing: Query-Heap + Readback-Buffer anlegen ===
  createGpuTimingResources();
  createTriangleCounterResources();

  updateUiDataStruct();
}

void SceneGraphViewerApp::onResize()
{
  createSRVandUAV();
}

void SceneGraphViewerApp::onDraw()
{

  // === DISPATCH THREAD SETTINGS ===

  constexpr UINT THREADS_X = 8;
  constexpr UINT THREADS_Y = 8;
  constexpr UINT THREADS_Z = 8;

  auto groupsX = (m_uiData.gridAdjusterX + THREADS_X - 1) / THREADS_X;
  auto groupsY = (m_uiData.gridAdjusterY + THREADS_Y - 1) / THREADS_Y;
  auto groupsZ = (m_uiData.gridAdjusterZ + THREADS_Z - 1) / THREADS_Z;

  // === END DISPATCH THREAD SETTINGS ===

  if (!ImGui::GetIO().WantCaptureMouse)
  {
    bool pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (pressed || released)
    {
      bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left);
      m_examinerController.click(pressed, left == true ? 1 : 2,
                                 ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl),
                                 getNormalizedMouseCoordinates());
    }
    else
    {
      m_examinerController.move(getNormalizedMouseCoordinates());
    }
  }

  const ComPtr<ID3D12GraphicsCommandList> commandList = getCommandList();
  const CD3DX12_CPU_DESCRIPTOR_HANDLE     rtvHandle   = getRTVHandle();
  const CD3DX12_CPU_DESCRIPTOR_HANDLE     dsvHandle   = getDSVHandle();

  // BEGIN TIMING MESSUNG
  const UINT frameIndex = getFrameIndex();
  BeginGpuTiming(frameIndex);

  Stamp(commandList.Get(), frameIndex, GpuMark::Frame_Begin);

  // default render target
  commandList->OMSetRenderTargets(1, &m_offscreenTarget_CPU_RTV, FALSE, &dsvHandle);

  const float clearColor[] = {m_uiData.backgroundColor.x, m_uiData.backgroundColor.y, m_uiData.backgroundColor.z, 1.0f};

  commandList->ClearRenderTargetView(m_offscreenTarget_CPU_RTV, clearColor, 0, nullptr);
  commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  commandList->RSSetViewports(1, &getViewport());
  commandList->RSSetScissorRects(1, &getRectScissor());

  drawScene(commandList);
  Stamp(commandList.Get(), frameIndex, GpuMark::Scene_Draw_End);
  // Transition RTV offscreen-target to UAV
  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    commandList->ResourceBarrier(_countof(barriers), barriers);
  }
  Stamp(commandList.Get(), frameIndex, GpuMark::Offscreen_To_UAV_End);
  // #MH Mesh shader test

  if (m_uiData.displayPostFog)
  {

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    ComPtr<ID3D12GraphicsCommandList6> commandList6;
    throwIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList6)));

    commandList6->SetPipelineState(m_meshPipelineState.Get());
    commandList6->SetGraphicsRootSignature(m_meshRootSignature.Get());

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_GridBufferA.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)};
    commandList6->ResourceBarrier(_countof(barriers), barriers);

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList6->SetGraphicsRootConstantBufferView(0, cb); // 0 add Constant Buffer for cam

    const gims::f32m4 cameraMatrix = m_examinerController.getTransformationMatrix();

    commandList6->SetGraphicsRoot32BitConstants(1, 16, &cameraMatrix, 0);

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_descriptorHeap.Get()}; // <- dein Heap mit SRVs
    commandList6->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList6->SetGraphicsRootDescriptorTable(2, m_edgeTable_GPU_SRV);

    commandList6->SetGraphicsRootDescriptorTable(3, m_gridBufferA_GPU_SRV); // t2 (LBCell-Buffer)

    commandList6->DispatchMesh(8, 8, 8);

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers3[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_GridBufferA.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    commandList6->ResourceBarrier(_countof(barriers3), barriers3);

    // DEBUGING

    commandList6->SetPipelineState(m_meshDebugPipelineState.Get());
    commandList6->SetGraphicsRootConstantBufferView(0, cb); // 0 add Constant Buffer for cam

    commandList6->SetGraphicsRoot32BitConstants(1, 16, &cameraMatrix, 0);

    commandList6->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList6->SetGraphicsRootDescriptorTable(2, m_edgeTable_GPU_SRV);

    commandList6->DispatchMesh(8, 8, 8);
  }

  // #MH COMPUTE 2D LBM
  if (m_uiData.displayLBM2D)
  {

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateHorizontal.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    //  GPU-Synchronisation**
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_gaussianTarget_x.Get());
    commandList->ResourceBarrier(1, &uavBarrier);

    // **GPU-Synchronisation**
    CD3DX12_RESOURCE_BARRIER uavBarrier2 = CD3DX12_RESOURCE_BARRIER::UAV(m_gaussianTarget_y.Get());
    commandList->ResourceBarrier(1, &uavBarrier2);
    {

      // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_gaussianTarget_y.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // ##### LBM Collision-Schritt
    commandList->SetPipelineState(m_computePipelineStateLBM.Get());
    commandList->SetComputeRootSignature(m_LBMcomputeRootSignature.Get());

    const int framesPerStep = m_uiData.debugSimSpeed;

    if (frameCounter++ % framesPerStep == 0)
    {
      Stamp(commandList.Get(), frameIndex, GpuMark::LBM2D_Begin);
      ID3D12Resource* readBuffer;
      ID3D12Resource* writeBuffer;

      // #MH doesnt mean just read or write, buffers are sswitching possition during compute passes
      readBuffer  = m_GridBufferA.Get();
      writeBuffer = m_GridBufferB.Get();

      commandList->SetComputeRootUnorderedAccessView(0, readBuffer->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(1, writeBuffer->GetGPUVirtualAddress());
      commandList->SetComputeRootDescriptorTable(2, m_finalTarget_GPU_UAV);
      commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer->GetGPUVirtualAddress()); // u3

      const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
      commandList->SetComputeRootConstantBufferView(4, cb); // 4 add Constant Buffer for lighting

      commandList->Dispatch(640 / 16, 480 / 16, 1);
      // mUseBufferA = !mUseBufferA;
      //    ##### LBM Streaming-Schritt

      Stamp(commandList.Get(), frameIndex, GpuMark::LBM2D_Collision_End);

      commandList->SetPipelineState(m_computePipelineStateLBMStream.Get());
      commandList->SetComputeRootUnorderedAccessView(
          0, writeBuffer->GetGPUVirtualAddress()); // read = was Collision geschrieben hat
      commandList->SetComputeRootUnorderedAccessView(1,
                                                     readBuffer->GetGPUVirtualAddress()); // write = zurück in alten
      commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer->GetGPUVirtualAddress()); // u3

      commandList->Dispatch(640 / 16, 480 / 16, 1);

      Stamp(commandList.Get(), frameIndex, GpuMark::LBM2D_Stream_End);
    }
    // ##### END LBM

    {
      // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_gaussianTarget_y.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
    Stamp(commandList.Get(), frameIndex, GpuMark::LBM2D_CopyToBackbuffer_End);
    // 8. Transition all targets back to render target

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  // #MH COMPUTE FLUID
  if (m_uiData.displayFluid)
  {

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateHorizontal.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    //  GPU-Synchronisation**
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_gaussianTarget_x.Get());
    commandList->ResourceBarrier(1, &uavBarrier);

    // **GPU-Synchronisation**
    CD3DX12_RESOURCE_BARRIER uavBarrier2 = CD3DX12_RESOURCE_BARRIER::UAV(m_gaussianTarget_y.Get());
    commandList->ResourceBarrier(1, &uavBarrier2);
    {

      // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_gaussianTarget_y.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // ##### LBM Collision-Schritt
    commandList->SetPipelineState(m_computePipelineStateLBM3D.Get());
    commandList->SetComputeRootSignature(m_LBM3DcomputeRootSignature.Get());

    const int framesPerStep = m_uiData.debugSimSpeed;

    if (frameCounter++ % framesPerStep == 0)
    {
      Stamp(commandList.Get(), frameIndex, GpuMark::LBM3D_Begin);

      ID3D12Resource* readBuffer;
      ID3D12Resource* writeBuffer;

      // #MH doesnt mean just read or write, buffers are sswitching possition during compute passes
      readBuffer  = m_GridBufferA3D.Get();
      writeBuffer = m_GridBufferB3D.Get();

      commandList->SetComputeRootUnorderedAccessView(0, readBuffer->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(1, writeBuffer->GetGPUVirtualAddress());
      /*commandList->SetComputeRootDescriptorTable(2, m_finalTarget_GPU_UAV);*/
      commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer3D->GetGPUVirtualAddress()); // u3

      const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
      commandList->SetComputeRootConstantBufferView(4, cb); // 4 add Constant Buffer for lighting

      commandList->Dispatch(groupsX, groupsY, groupsZ);

      Stamp(commandList.Get(), frameIndex, GpuMark::LBM3D_Collision_End);

      /*mUseBufferA = !mUseBufferA;*/
      //    ##### LBM Streaming-Schritt
      commandList->SetPipelineState(m_computePipelineStateLBM3DStream.Get());
      commandList->SetComputeRootUnorderedAccessView(
          0, writeBuffer->GetGPUVirtualAddress()); // read = was Collision geschrieben hat
      commandList->SetComputeRootUnorderedAccessView(1, readBuffer->GetGPUVirtualAddress()); // write = zurück in alten
      commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer3D->GetGPUVirtualAddress()); // u3

      commandList->Dispatch(groupsX, groupsY, groupsZ);

      Stamp(commandList.Get(), frameIndex, GpuMark::LBM3D_Stream_End);

      /*mUseBufferA = !mUseBufferA;*/
      //    ##### LBM CELLTYPE-Schritt
      commandList->SetPipelineState(m_computePipelineStateLBM3DUpdateCellTypes.Get());

      commandList->SetComputeRootUnorderedAccessView(0, readBuffer->GetGPUVirtualAddress()); // write = zurück in alten
      commandList->SetComputeRootUnorderedAccessView(1, m_CellTypeBuffer3D->GetGPUVirtualAddress()); // u3
      commandList->SetComputeRootConstantBufferView(4, cb); // 4 add Constant Buffer for lighting

      commandList->Dispatch(groupsX, groupsY, groupsZ);

      Stamp(commandList.Get(), frameIndex, GpuMark::LBM3D_UpdateCellTypes_End);
    }
    // ##### END LBM

    {
      // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_gaussianTarget_y.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
    Stamp(commandList.Get(), frameIndex, GpuMark::LBM3D_CopyToBackbuffer_End);

    // 8. Transition all targets back to render target

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  // # LBMMC TEST
  if (m_uiData.displayLBMMC)
  {

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    ComPtr<ID3D12GraphicsCommandList6> commandList6;
    throwIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList6)));

    Stamp(commandList6.Get(), frameIndex, GpuMark::LBMMC_Begin);

    commandList6->SetPipelineState(m_meshPipelineState.Get());
    commandList6->SetGraphicsRootSignature(m_meshRootSignature.Get());

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_CellTypeBuffer3D.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ)};
    commandList6->ResourceBarrier(_countof(barriers), barriers);

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList6->SetGraphicsRootConstantBufferView(0, cb); // 0 add Constant Buffer for cam

    const gims::f32m4 cameraMatrix = m_examinerController.getTransformationMatrix();

    commandList6->SetGraphicsRoot32BitConstants(1, 16, &cameraMatrix, 0);

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_descriptorHeap.Get()}; // <- Heap mit SRVs
    commandList6->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList6->SetGraphicsRootDescriptorTable(2, m_edgeTable_GPU_SRV);
    commandList6->SetGraphicsRootDescriptorTable(3, m_cellTypeBufferA3D_GPU_SRV); // t2 (LBCell-Buffer)
    commandList6->SetGraphicsRootDescriptorTable(4, m_triCountUAV_GPU_UAV);

    // TRIANGLE COUNTER BEGIN

    //

    // // COMMON -> COPY_DEST (nur im ersten Frame nötig)
    auto bInit = CD3DX12_RESOURCE_BARRIER::Transition(m_triCountUAV.Get(), D3D12_RESOURCE_STATE_COMMON,
                                                      D3D12_RESOURCE_STATE_COPY_DEST);
    commandList6->ResourceBarrier(1, &bInit);

    // Reset counter to 0 via Copy from upload
    commandList6->CopyBufferRegion(m_triCountUAV.Get(), 0, m_triZeroUpload.Get(), 0, 4);
    {
      // UAV state für Mesh-Shader
      auto barrierCount = CD3DX12_RESOURCE_BARRIER::Transition(m_triCountUAV.Get(), D3D12_RESOURCE_STATE_COPY_DEST,

                                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      commandList6->ResourceBarrier(1, &barrierCount);
    }
    // COUNTER END


    commandList6->DispatchMesh(m_uiData.gridAdjusterX, m_uiData.gridAdjusterY, m_uiData.gridAdjusterZ);
    Stamp(commandList6.Get(), frameIndex, GpuMark::LBMMC_Dispatch_End);


    // BEGIN TRIANGLE COUNT
    // zurück in COPY_SOURCE
    {
      auto barrierCountTwo = CD3DX12_RESOURCE_BARRIER::Transition(
          m_triCountUAV.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

      commandList6->ResourceBarrier(1, &barrierCountTwo);
    }

    // Kopieren
    commandList6->CopyResource(m_triCountReadback.Get(), m_triCountUAV.Get());

    // zurück für nächsten Frame (COPY_DEST)
    {

      auto barrierCountThree = CD3DX12_RESOURCE_BARRIER::Transition(
          m_triCountUAV.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

      commandList6->ResourceBarrier(1, &barrierCountThree);
    }

    // END TRIANGLE COUNT

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers3[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_CellTypeBuffer3D.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    commandList6->ResourceBarrier(_countof(barriers3), barriers3);

    // DEBUGING
    /*
    commandList6->SetPipelineState(m_meshDebugPipelineState.Get());
    commandList6->SetGraphicsRootConstantBufferView(0, cb); // 0 add Constant Buffer for cam

    commandList6->SetGraphicsRoot32BitConstants(1, 16, &cameraMatrix, 0);

    commandList6->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList6->SetGraphicsRootDescriptorTable(2, m_edgeTable_GPU_SRV);

    commandList6->DispatchMesh(128, 96, 64);
    */
  }

  if (m_uiData.displayDebugPlane)
  {
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    Stamp(commandList.Get(), frameIndex, GpuMark::DebugPlane_Begin);

    if (m_uiData.displayDebugRho)
    {
      commandList->SetGraphicsRootSignature(m_DebugQuadRootSig.Get());
      commandList->SetPipelineState(m_debugPlanepipelineStateRho.Get());
      commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    }
    else if (m_uiData.displayDebugCelltype)
    {
      commandList->SetGraphicsRootSignature(m_DebugQuadRootSig.Get());
      commandList->SetPipelineState(m_debugPlanepipelineStateCelltype.Get());
      commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    }
    else if (m_uiData.displayDebugM)
    {
      commandList->SetGraphicsRootSignature(m_DebugQuadRootSig.Get());
      commandList->SetPipelineState(m_debugPlanepipelineStateM.Get());
      commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    }
    else if (m_uiData.displayDebugEpsilon)
    {
      commandList->SetGraphicsRootSignature(m_DebugQuadRootSig.Get());
      commandList->SetPipelineState(m_debugPlanepipelineStateEpsilon.Get());
      commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    }
    else
    {
      commandList->SetGraphicsRootSignature(m_DebugQuadRootSig.Get());
      commandList->SetPipelineState(m_debugPlanepipelineStateCelltype.Get());
      commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());
    }

    commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->RSSetViewports(1, &getViewport());
    commandList->RSSetScissorRects(1, &getRectScissor());

    commandList->IASetVertexBuffers(0, 1, &m_DebugVBView);
    commandList->IASetIndexBuffer(&m_DebugIBView);

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(0, cb); // 0 add Constant Buffer for cam

    const gims::f32m4 cameraMatrix = m_examinerController.getTransformationMatrix();

    commandList->SetGraphicsRoot32BitConstants(2, 16, &cameraMatrix, 0);
    // commandList->SetComputeRootSignature(m_LBM3DcomputeRootSignature.Get());

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers5[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_CellTypeBuffer3D.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
    commandList->ResourceBarrier(_countof(barriers5), barriers5);

    commandList->SetGraphicsRootDescriptorTable(3, m_cellTypeBufferA3D_GPU_SRV); // t2
    commandList->SetGraphicsRootUnorderedAccessView(4, m_GridBufferA3D->GetGPUVirtualAddress());

    commandList->DrawIndexedInstanced(m_DebugIndexCount, 1, 0, 0, 0);
    Stamp(commandList.Get(), frameIndex, GpuMark::DebugPlane_Draw_End);

    // **GPU-Synchronisation: `m_finalTarget` als SRV vorbereiten**
    CD3DX12_RESOURCE_BARRIER barriers6[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_CellTypeBuffer3D.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    commandList->ResourceBarrier(_countof(barriers6), barriers6);
  }

  // #MH RENDERING CLEAR IMAGE
  if (!m_uiData.displayPostDOF && !m_uiData.displayPostFog && !m_uiData.displayLBMMC && !m_uiData.displayDebugPlane &&
      !m_uiData.displayLBM2D)
  {

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_offscreenTarget.Get());

    // 8. Transition all targets back to render target
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  // Transition RTV offscreen-target to UAV
  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET)};
    commandList->ResourceBarrier(_countof(barriers), barriers);
  }

  // END TIMING MESSUNG
  Stamp(commandList.Get(), frameIndex, GpuMark::Frame_End);
  EndGpuTiming(commandList.Get(), frameIndex); // <-- resolvt nur m_tsCursor[frameIndex]

#if 0

#endif

  // set window as render target
  commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
}

void SceneGraphViewerApp::onDrawUI()
{
  const ImGuiWindowFlags_ imGuiFlags =
      m_examinerController.active() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;

  // GPU TIMING ABHOLEN
  collectGpuTimes();
  queryVram();
  collectMeshStats();


  // Fenster/Section für Timings
  ImGui::Begin("GPU Timings"); // oder packe es in dein "Information"-Fenster als CollapsingHeader

  // Queue-Frequenz + total
  const double gpuFrameMs = m_gpuMs[static_cast<UINT>(GpuMark::Frame_End)]; // von dtMs(Frame_Begin, Frame_End)
  ImGui::Text("Queue freq: %.3f MHz", double(m_tsFrequency) / 1e6);
  if (!std::isnan(gpuFrameMs))
    ImGui::Text("GPU Frame time: %.3f ms (%.1f FPS GPU)", gpuFrameMs, 1000.0 / gpuFrameMs);

  // Kleine Historie (letzte 240 Frames)
  {
    static float hist[240] = {};
    static int   idx       = 0;
    if (!std::isnan(gpuFrameMs))
    {
      hist[idx] = static_cast<float>(gpuFrameMs);
      idx       = (idx + 1) % IM_ARRAYSIZE(hist);
    }
    ImGui::PlotLines("GPU Frame ms", hist, IM_ARRAYSIZE(hist), idx, nullptr, 0.0f, 40.0f, ImVec2(0, 60));
  }

  // Tabelle mit Segmenten
  if (ImGui::BeginTable("gpu_segments", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
  {
    ImGui::TableSetupColumn("Segment");
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableHeadersRow();

    auto row = [&](const char* label, GpuMark endMark)
    {
      const double ms = m_gpuMs[static_cast<UINT>(endMark)];
      if (std::isnan(ms))
        return; // in diesem Frame nicht vorhanden
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(label);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.3f", ms);
    };

    row("Scene Draw", GpuMark::Scene_Draw_End);
    row("RTV->UAV Transition", GpuMark::Offscreen_To_UAV_End);

    row("PostFog Mesh", GpuMark::PostFog_Mesh_End);
    row("PostFog Debug Mesh", GpuMark::PostFog_Debug_End);

    row("LBM2D Collision", GpuMark::LBM2D_Collision_End);
    row("LBM2D Streaming", GpuMark::LBM2D_Stream_End);
    row("LBM2D CopyToBackbuffer", GpuMark::LBM2D_CopyToBackbuffer_End);

    row("LBM3D Collision", GpuMark::LBM3D_Collision_End);
    row("LBM3D Streaming", GpuMark::LBM3D_Stream_End);
    row("LBM3D UpdateCellTypes", GpuMark::LBM3D_UpdateCellTypes_End);
    row("LBM3D CopyToBackbuffer", GpuMark::LBM3D_CopyToBackbuffer_End);

    row("LBMMC DispatchMesh", GpuMark::LBMMC_Dispatch_End);

    row("DebugPlane Draw", GpuMark::DebugPlane_Draw_End);

    ImGui::EndTable();
  }
  // MLUPS ausgabe
  if (!std::isnan(m_mlups))
    ImGui::Text("MLUPS (LBM3D): %.2f", m_mlups);

  if (!std::isnan(m_vramUsageGiB) && !std::isnan(m_vramBudgetGiB))
  {
    ImGui::Separator();
    ImGui::Text("VRAM: %.2f / %.2f GiB", m_vramUsageGiB, m_vramBudgetGiB);
    ImGui::ProgressBar(float(m_vramUsageGiB / m_vramBudgetGiB), ImVec2(-1, 0));
  }

  // Optional: simple „Budget-Balken“ gegen 16.67 ms
  if (!std::isnan(gpuFrameMs))
  {
    ImGui::Spacing();
    ImGui::Text("Budget (16.67 ms):");
    ImGui::ProgressBar(static_cast<float>(gpuFrameMs / 16.667), ImVec2(-1, 0));
  }

  ImGui::Separator();
  ImGui::Text("Triangles (this frame): %.3f M", double(m_triCountFrame) / 1e6);
  if (!std::isnan(m_triAvg))
    ImGui::Text("Triangles avg (last %d): %.3f M", kTriHistory, m_triAvg);
  ImGui::Text("Triangles max (last %d): %.3f M", kTriHistory, m_triMax);

  ImGui::PlotLines("Triangles [M]", m_triHist, IM_ARRAYSIZE(m_triHist), m_triHistIdx, nullptr, 0.0f,
                   float(std::max(1.0, m_triMax * 1.2)), ImVec2(0, 60));


  ImGui::End();

  // END GPU TIMING VISUALISATION

  // Information Window
  ImGui::Begin("Information", nullptr, imGuiFlags);
  ImGui::Text("Frametime: %f ms", 1.0f / ImGui::GetIO().Framerate * 1000.0f);
  gims::f32v3 cameraPosition = getCameraPosition();
  ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", cameraPosition.x, cameraPosition.y, cameraPosition.z);
  ImGui::Checkbox("Display Scene Data", &m_uiData.displaySceneData);
  if (m_uiData.displaySceneData)
  {
    ImGui::Text("Number of Nodes in Scene: %i", m_uiData.numberOfNodes);
    ImGui::Text("Number of Meshes in Scene: %i", m_uiData.numberOfMeshes);
    ImGui::Text("Number of Materials loaded: %i", m_uiData.numberOfMaterials);
    ImGui::Text("Number of Textures loaded: %i", m_uiData.numberOfTextures);
    ImGui::Text("Scene AABB Lower Left: (%.5f, %.5f, %.5f)", m_uiData.sceneLowerleftAABBPosition.x,
                m_uiData.sceneLowerleftAABBPosition.y, m_uiData.sceneLowerleftAABBPosition.z);
    ImGui::Text("Scene AABB Top Right: (%.5f, %.5f, %.5f)", m_uiData.sceneTopRightAABBPosition.x,
                m_uiData.sceneTopRightAABBPosition.y, m_uiData.sceneTopRightAABBPosition.z);
  }
  ImGui::End(); // End of Information Window

  // Configuration Window
  ImGui::Begin("Configuration", nullptr, imGuiFlags);
  ImGui::ColorEdit3("Background Color", &m_uiData.backgroundColor[0]);       // Background Color
  ImGui::Checkbox("Display Bounding Boxes", &m_uiData.displayBoundingBoxes); // BoundingBoxes
  if (m_uiData.displayBoundingBoxes)
  {
    ImGui::ColorEdit3("Bounding Box Color", &m_uiData.boundingBoxColor[0]); // Bounding Box Color
  }
  ImGui::SliderInt("Number of Lights", &m_uiData.numOfLightsInScene, 1, 8); // Number of Lights
  for (int i = 0; i < m_uiData.numOfLightsInScene; ++i)                     // Light Properties
  {
    std::string lightLabel = "Light " + std::to_string(i + 1);
    if (ImGui::CollapsingHeader(lightLabel.c_str()))
    {
      // Position
      std::string positionLabel = "Position##" + std::to_string(i);
      ImGui::DragFloat3(positionLabel.c_str(), &m_Lights[i].lightPosition[0], 0.1f);
      // Color
      std::string colorLabel = "Color##" + std::to_string(i);
      ImGui::ColorEdit3(colorLabel.c_str(), &m_Lights[i].lightColor[0]);
      // Intensity
      std::string intensityLabel = "Intensity##" + std::to_string(i);
      ImGui::SliderFloat(intensityLabel.c_str(), &m_Lights[i].lightIntensity, 0.0f, 10.0f);
    }
  }

  std::string lbm2DLabel = "2D LBM";
  if (ImGui::CollapsingHeader(lbm2DLabel.c_str()))
  {
    ImGui::Checkbox("Display LBM 2D", &m_uiData.displayLBM2D);
  }

  std::string debug3Dlabel = "Debug LBM 3D";
  if (ImGui::CollapsingHeader(debug3Dlabel.c_str()))
  {
    // Active
    ImGui::Checkbox("Display Debug Planes", &m_uiData.displayDebugPlane);

    ImGui::SliderInt("Plane location", &m_uiData.zSlicePosition, 0, 64);

    ImGui::Checkbox("View LBM CellType", &m_uiData.displayDebugCelltype);
    ImGui::Checkbox("View LBM rho", &m_uiData.displayDebugRho);
    ImGui::Checkbox("View LBM m", &m_uiData.displayDebugM);
    ImGui::Checkbox("View LBM epsilon", &m_uiData.displayDebugEpsilon);
  }

  std::string simlabel = "Simulation settings";
  if (ImGui::CollapsingHeader(simlabel.c_str()))
  {

    ImGui::SliderInt("Simulation step per Frame", &m_uiData.debugSimSpeed, 1, 200);

    ImGui::Text("LBM 3D config:");
    ImGui::SliderFloat("Color intensity", &m_uiData.colorValue, 0.01f, 1.0f);
    ImGui::SliderFloat("Inflow speed", &m_uiData.inflowSpeed, 0.0f, 10.0f);
    ImGui::SliderFloat("Outflow speed", &m_uiData.outflowSpeed, 0.0f, 10.0f);
    ImGui::SliderFloat("LBM Tau", &m_uiData.lbmTau, 0.8f, 15.6f);

    if (ImGui::Button("Reset stream"))
    {
      // Statische Variable, damit die Eingabewerte zwischen Frames erhalten bleiben.
      InitializeLBMGridWithStreifen();
      InitializeLBMGrid3D();
    }
    if (ImGui::Button("Reset stream v2"))
    {
      InitializeLBMGrid3D_v2();
    }
  }

  std::string domainLabel = "Domain Size";
  if (ImGui::CollapsingHeader(domainLabel.c_str()))
  {

    ImGui::Text("Grid Size:");
    ImGui::SliderInt("Size - X", &m_uiData.gridAdjusterX, 128, 400);
    ImGui::SliderInt("Size - Y", &m_uiData.gridAdjusterY, 96, 250);
    ImGui::SliderInt("Size - Z", &m_uiData.gridAdjusterZ, 64, 200);

    if (ImGui::Button("Accept changes:"))
    {
      // new init after changing size of grid
      createSRVandUAV();
      CreateLBM3DBuffers();
      InitializeLBMGrid3D();
    }
  }

  // Fluid LBM setup
  ImGui::Text("Fluid LBM:");
  ImGui::Checkbox("Active Simulation", &m_uiData.displayFluid);
  ImGui::Checkbox("Display MESH", &m_uiData.displayLBMMC);

  std::string camlabel = "Camera settings";
  if (ImGui::CollapsingHeader(camlabel.c_str()))
  {
    ImGui::SliderFloat("Near Plane", &m_uiData.nearPlane, 0.0f, 16.0f);
    ImGui::SliderFloat("Far Plane", &m_uiData.farPlane, 0.0f, 10000.0f);
    if (ImGui::Button("Get Translation"))
    {
      // Annahme: f32v3 hat die Mitglieder x, y, und z.
      f32v3 currentTranslation = m_examinerController.getTranslationVector();
      std::cout << "Aktuelle Kamera-Position: (" << currentTranslation.x << ", " << currentTranslation.y << ", "
                << currentTranslation.z << ")" << std::endl;
    }

    if (ImGui::Button("Get Rotation"))
    {
      f32q currentRotation = m_examinerController.getRotationQuaterion();
      std::cout << "Aktuelle Kamera-Rotation (Quaternion): (" << currentRotation.x << ", " << currentRotation.y << ", "
                << currentRotation.z << ", " << currentRotation.w << ")" << std::endl;
    }
    if (ImGui::Button("Cam Preset 2"))
    {
      // Statische Variable, damit die Eingabewerte zwischen Frames erhalten bleiben.
      static float translation[3] = {-5.92f, -61.1f, 241.9f};
      ImGui::InputFloat3("Neue Translation", translation);
      // Umwandlung in deinen f32v3-Typ, falls nötig.
      f32v3 newTranslation = {translation[0], translation[1], translation[2]};
      m_examinerController.setTranslationVector(newTranslation);

      static float rotation[4] = {0.876f, -0.10f, -0.47f, 0.05f};
      ImGui::InputFloat4("Neue Rotation (Quaternion)", rotation);
      // Umwandlung in deinen f32q-Typ, falls nötig.
      f32q newRotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
      m_examinerController.setRotationQuaterion(newRotation);
    }
    ImGui::Text("Set Cam Presets:");
  }

  if (ImGui::Button("Defaults")) // Reseting changed UiData
  {
    m_uiData = UiData();
    updateUiDataStruct();
  }
  ImGui::End(); // Emd of Configuration Window
}

void SceneGraphViewerApp::createGraphicRootSignature()
{

  // Initialize the sampler (s0)
  CD3DX12_STATIC_SAMPLER_DESC samplerDesc(0);
  samplerDesc.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samplerDesc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplerDesc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplerDesc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplerDesc.MipLODBias       = 0;
  samplerDesc.MaxAnisotropy    = 0;
  samplerDesc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
  samplerDesc.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  samplerDesc.MinLOD           = 0.0f;
  samplerDesc.MaxLOD           = D3D12_FLOAT32_MAX;
  samplerDesc.ShaderRegister   = 0;
  samplerDesc.RegisterSpace    = 0;
  samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  {

    // Root parameters for main rendering
    CD3DX12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].InitAsConstantBufferView(0); // PerFrameConstants (b0)
    rootParameters[1].InitAsConstants(16, 1);      // PerMeshConstants (b1)
    rootParameters[2].InitAsConstantBufferView(2); // Material (b2)

    CD3DX12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0); // 5 textures starting at t0
    rootParameters[3].InitAsDescriptorTable(1, &srvRange);
    // Main root signature description
    CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = {};
    descRootSignature.Init(_countof(rootParameters), rootParameters, 1, &samplerDesc,
                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    // Serialize and create main root signature
    ComPtr<ID3DBlob> rootBlob, errorBlob;
    if (FAILED(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob)))
    {
      if (errorBlob)
        OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
      throw std::runtime_error("Failed to serialize root signature.");
    }

    if (FAILED(getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_rootSignature))))
    {
      throw std::runtime_error("Failed to create root signature.");
    }
  }
  {
    // Root parameters for depth rendering
    CD3DX12_ROOT_PARAMETER   rootParametersDepth[1] = {};
    CD3DX12_DESCRIPTOR_RANGE srvRangeDepth          = {};
    srvRangeDepth.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // 1 texture starting at t0
    rootParametersDepth[0].InitAsDescriptorTable(1, &srvRangeDepth);
    // Depth root signature description
    CD3DX12_ROOT_SIGNATURE_DESC descRootSignatureDepth = {};
    descRootSignatureDepth.Init(_countof(rootParametersDepth), rootParametersDepth, 1, &samplerDesc,
                                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    // Serialize and create depth root signature
    ComPtr<ID3DBlob> depthRootBlob, depthErrorBlob;
    if (FAILED(D3D12SerializeRootSignature(&descRootSignatureDepth, D3D_ROOT_SIGNATURE_VERSION_1, &depthRootBlob,
                                           &depthErrorBlob)))
    {
      if (depthErrorBlob)
        OutputDebugStringA(static_cast<char*>(depthErrorBlob->GetBufferPointer()));
      throw std::runtime_error("Failed to serialize depth root signature.");
    }
    if (FAILED(getDevice()->CreateRootSignature(0, depthRootBlob->GetBufferPointer(), depthRootBlob->GetBufferSize(),
                                                IID_PPV_ARGS(&m_rootSignatureDepth))))
    {
      throw std::runtime_error("Failed to create depth root signature.");
    }
  }
}

void SceneGraphViewerApp::createDebugQuadRootSignature()
{

  CD3DX12_ROOT_PARAMETER slotRootParameter[5];

  CD3DX12_DESCRIPTOR_RANGE srvRange;
  srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // u2

  // Root Parameter 0 – Constant Buffer View (CBV) für Slice-Parameter (b0)
  slotRootParameter[0].InitAsConstantBufferView(0);

  // Root Parameter 1 – Shader Resource View (SRV) für 3D-Textur (t0)
  slotRootParameter[1].InitAsShaderResourceView(0);

  // Slot 2: ViewProj-Matrix als 16 Floats → b1
  slotRootParameter[2].InitAsConstants(16, 1);

  slotRootParameter[3].InitAsDescriptorTable(1, &srvRange); // t2 (LBCell-SRV)

  slotRootParameter[4].InitAsUnorderedAccessView(0);

  // Keine statischen Sampler nötig!
  CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
                                          0,       // 0 static samplers
                                          nullptr, // keine statischen Sampler
                                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  // Serialize and create main root signature
  ComPtr<ID3DBlob> rootBlob, errorBlob;
  if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob)))
  {
    if (errorBlob)
      OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
    throw std::runtime_error("Failed to serialize root signature.");
  }

  if (FAILED(getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&m_DebugQuadRootSig))))
  {
    throw std::runtime_error("Failed to create root signature.");
  }
}

void SceneGraphViewerApp::createGraphicPipeline()
{
  waitForGPU();
  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs = TriangleMeshD3D12::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescsBB = BoundingBox::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescsQuad = FullScreenQuad::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementPT = FullScreenQuad::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementPostProcessing = FullScreenQuad::getInputElementDescriptors();

  const ComPtr<IDxcBlob> vertexShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/TriangleMesh.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/TriangleMesh.hlsl", L"PS_main", L"ps_6_0");
  const ComPtr<IDxcBlob> vertexShaderBB =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/BoundingBox.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderBB =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/BoundingBox.hlsl", L"PS_main", L"ps_6_0");
  const ComPtr<IDxcBlob> vertexShaderDepth =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/DepthBuffer.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderDepth =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/DepthBuffer.hlsl", L"PS_main", L"ps_6_0");
  // const ComPtr<IDxcBlob> vertexShaderQuad =
  //     compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/FullScreenQuad.hlsl", L"VS_main", L"vs_6_0");
  // const ComPtr<IDxcBlob> pixelShaderQuad =
  //     compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/FullScreenQuad.hlsl", L"PS_main", L"ps_6_0");

  // #MH PassThrough shader configuration
  const ComPtr<IDxcBlob> vertexShaderPT =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PassThrough.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderPT =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PassThrough.hlsl", L"PS_main", L"ps_6_0");

  // #MH Post processing shader configuration
  const ComPtr<IDxcBlob> vertexShaderPP =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PostProcess.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderPP =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PostProcess.hlsl", L"PS_main", L"ps_6_0");

  // DEFINE Renderpipeline
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout                        = {inputElementDescs.data(), (ui32)inputElementDescs.size()};
  psoDesc.pRootSignature                     = m_rootSignature.Get();
  psoDesc.VS                                 = HLSLCompiler::convert(vertexShader);
  psoDesc.PS                                 = HLSLCompiler::convert(pixelShader);
  psoDesc.RasterizerState                    = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.FillMode           = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode           = D3D12_CULL_MODE_NONE;
  psoDesc.BlendState                         = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DSVFormat                          = getDX12AppConfig().depthBufferFormat;
  psoDesc.DepthStencilState.DepthEnable      = TRUE;
  psoDesc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS;
  psoDesc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.StencilEnable    = FALSE;
  psoDesc.SampleMask                         = UINT_MAX;
  psoDesc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets                   = 1;
  psoDesc.RTVFormats[0]                      = getDX12AppConfig().renderTargetFormat;
  psoDesc.SampleDesc.Count                   = 1;
  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

  // DEFINE BoundingBox Pipeline
  psoDesc.InputLayout              = {inputElementDescsBB.data(), (ui32)inputElementDescsBB.size()};
  psoDesc.VS                       = HLSLCompiler::convert(vertexShaderBB);
  psoDesc.PS                       = HLSLCompiler::convert(pixelShaderBB);
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
  psoDesc.PrimitiveTopologyType    = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateBB)));

  // DEFINE Depth Quad Pipeline
  psoDesc.InputLayout                      = {inputElementDescsQuad.data(), (ui32)inputElementDescsQuad.size()};
  psoDesc.pRootSignature                   = m_rootSignatureDepth.Get();
  psoDesc.VS                               = HLSLCompiler::convert(vertexShaderDepth);
  psoDesc.PS                               = HLSLCompiler::convert(pixelShaderDepth);
  psoDesc.RasterizerState.FillMode         = D3D12_FILL_MODE_SOLID;
  psoDesc.PrimitiveTopologyType            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.DepthStencilState.DepthEnable    = FALSE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.NumRenderTargets                 = 1;
  psoDesc.RTVFormats[0]                    = getDX12AppConfig().renderTargetFormat;
  psoDesc.SampleDesc.Count                 = 1;
  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateDepthTexture)));
}

void SceneGraphViewerApp::createDebugQuadPipeline()
{
  waitForGPU();
  std::vector<D3D12_INPUT_ELEMENT_DESC> debugQuadInputLayout = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs = debugQuadInputLayout;

  const ComPtr<IDxcBlob> vertexShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/VSDebugLBM3D.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderCelltype =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PSDebugLBM3D.hlsl", L"PS_celltype", L"ps_6_0");

  // DEFINE Renderpipeline
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout                        = {debugQuadInputLayout.data(), (UINT)debugQuadInputLayout.size()};
  psoDesc.pRootSignature                     = m_DebugQuadRootSig.Get();
  psoDesc.VS                                 = HLSLCompiler::convert(vertexShader);
  psoDesc.PS                                 = HLSLCompiler::convert(pixelShaderCelltype);
  psoDesc.RasterizerState                    = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.FillMode           = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode           = D3D12_CULL_MODE_NONE;
  psoDesc.BlendState                         = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DSVFormat                          = getDX12AppConfig().depthBufferFormat;
  psoDesc.DepthStencilState.DepthEnable      = FALSE;
  psoDesc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS;
  psoDesc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.StencilEnable    = FALSE;
  psoDesc.SampleMask                         = UINT_MAX;
  psoDesc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets                   = 1;
  psoDesc.RTVFormats[0]                      = getDX12AppConfig().renderTargetFormat;
  psoDesc.SampleDesc.Count                   = 1;

  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_debugPlanepipelineStateCelltype)));

  const ComPtr<IDxcBlob> pixelShaderRho =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PSDebugLBM3D.hlsl", L"PS_rho", L"ps_6_0");

  psoDesc.PS = HLSLCompiler::convert(pixelShaderRho);

  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_debugPlanepipelineStateRho)));

  const ComPtr<IDxcBlob> pixelShaderM =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PSDebugLBM3D.hlsl", L"PS_m", L"ps_6_0");

  psoDesc.PS = HLSLCompiler::convert(pixelShaderM);

  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_debugPlanepipelineStateM)));

  const ComPtr<IDxcBlob> pixelShaderEpsilon =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/PSDebugLBM3D.hlsl", L"PS_eps", L"ps_6_0");

  psoDesc.PS = HLSLCompiler::convert(pixelShaderEpsilon);

  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_debugPlanepipelineStateEpsilon)));
}

void SceneGraphViewerApp::drawScene(const ComPtr<ID3D12GraphicsCommandList>& cmdLst)
{
  const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
  const gims::f32m4               cameraMatrix = m_examinerController.getTransformationMatrix();

  updateSceneConstantBuffer();

  cmdLst->SetPipelineState(m_pipelineState.Get());
  cmdLst->SetGraphicsRootSignature(m_rootSignature.Get());
  cmdLst->SetGraphicsRootConstantBufferView(0, cb);

  gims::f32m4 normalizedSceneTransform = m_scene.getAABB().getNormalizationTransformation();
  gims::f32m4 transform                = cameraMatrix * normalizedSceneTransform;

  // m_scene.addToCommandList(cmdLst, transform, 1, 2, 3);

  if (m_uiData.displayBoundingBoxes)
  {
    cmdLst->SetPipelineState(m_pipelineStateBB.Get());

    m_scene.addToCommandListBoundingBox(cmdLst, transform, 1, 2, 3);
  }
}

void SceneGraphViewerApp::createSceneConstantBuffer()
{
  const ConstantBuffer cb         = {};
  const gims::ui64     frameCount = getDX12AppConfig().frameCount;
  m_Lights[0].lightPosition       = gims::f32v3(2.0f, 4.0f, 1.0f);
  m_Lights[0].lightIntensity      = gims::f32(1.0f);
  m_constantBuffers.resize(frameCount);

  for (gims::ui32 i = 0; i < frameCount; i++)
  {
    m_constantBuffers[i] = ConstantBufferD3D12(cb, getDevice());
  }
}

void SceneGraphViewerApp::updateSceneConstantBuffer()
{

  ConstantBuffer cb   = {};
  cb.projectionMatrix = glm::perspectiveFovLH_ZO<gims::f32>(
      glm::radians(45.0f), (gims::f32)getWidth(), (gims::f32)getHeight(), m_uiData.nearPlane, m_uiData.farPlane);
  cb.cameraPosition = getCameraPosition();
  cb.numOfLights    = m_uiData.numOfLightsInScene;
  for (gims::ui8 i = 0; i < 8; i++)
  {
    cb.Lights[i] = m_Lights[i];
  }
  cb.boundingBoxColor = m_uiData.boundingBoxColor;
  cb.focalDistance    = m_uiData.focalDistance;
  cb.blurStrength     = m_uiData.blurStrength;
  cb.blurRadius       = m_uiData.blurRadius;
  cb.nearPlane        = m_uiData.nearPlane;
  cb.farPlane         = m_uiData.farPlane;
  cb.vigValue         = m_uiData.vignetIntes;
  cb.grainValue       = m_uiData.grainIntes;

  cb.colorIntens = m_uiData.colorValue;
  cb.inflow_u    = m_uiData.inflowSpeed;
  cb.outflow_u   = m_uiData.outflowSpeed;

  cb.tau = m_uiData.lbmTau;

  cb.debugPlanePos = m_uiData.zSlicePosition;

  cb.gridSizeX = m_uiData.gridAdjusterX;
  cb.gridSizeY = m_uiData.gridAdjusterY;
  cb.gridSizeZ = m_uiData.gridAdjusterZ;

  m_constantBuffers[getFrameIndex()].upload(&cb);
}

void SceneGraphViewerApp::createLBMconstantBuffer()
{
  const ConstantBuffer cbLBM      = {};
  const gims::ui64     frameCount = getDX12AppConfig().frameCount;
  m_Lights[0].lightPosition       = gims::f32v3(2.0f, 4.0f, 1.0f);
  m_Lights[0].lightIntensity      = gims::f32(1.0f);
  m_constantBuffersLBM.resize(frameCount);

  for (gims::ui32 i = 0; i < frameCount; i++)
  {
    m_constantBuffersLBM[i] = ConstantBufferD3D12(cbLBM, getDevice());
  }
}

void SceneGraphViewerApp::updateLBMconstantBuffer()
{
  ConstantBuffer cbLBM   = {};
  cbLBM.projectionMatrix = glm::perspectiveFovLH_ZO<gims::f32>(
      glm::radians(45.0f), (gims::f32)getWidth(), (gims::f32)getHeight(), m_uiData.nearPlane, m_uiData.farPlane);
  cbLBM.cameraPosition = getCameraPosition();
  cbLBM.numOfLights    = m_uiData.numOfLightsInScene;
  for (gims::ui8 i = 0; i < 8; i++)
  {
    cbLBM.Lights[i] = m_Lights[i];
  }
  cbLBM.boundingBoxColor = m_uiData.boundingBoxColor;
  cbLBM.focalDistance    = m_uiData.focalDistance;
  cbLBM.blurStrength     = m_uiData.blurStrength;
  cbLBM.blurRadius       = m_uiData.blurRadius;
  cbLBM.nearPlane        = m_uiData.nearPlane;
  cbLBM.farPlane         = m_uiData.farPlane;
  cbLBM.vigValue         = m_uiData.vignetIntes;
  cbLBM.grainValue       = m_uiData.grainIntes;

  m_constantBuffersLBM[getFrameIndex()].upload(&cbLBM);
}

gims::f32v3 SceneGraphViewerApp::getCameraPosition()
{
  gims::f32m4 invertedCameraMatrix = glm::inverse(m_examinerController.getTransformationMatrix());

  return gims::f32v3(invertedCameraMatrix[3][0], invertedCameraMatrix[3][1], invertedCameraMatrix[3][2]);
}

void SceneGraphViewerApp::updateUiDataStruct()
{
  m_uiData.numberOfNodes              = m_scene.getNumberOfNodes();
  m_uiData.numberOfMeshes             = m_scene.getNumberOfMeshes();
  m_uiData.numberOfMaterials          = m_scene.getNumberOfMaterials();
  m_uiData.numberOfTextures           = m_scene.getNumberOfTextures() - 3;
  m_uiData.sceneLowerleftAABBPosition = m_scene.getAABB().getLowerLeftBottom();
  m_uiData.sceneTopRightAABBPosition  = m_scene.getAABB().getUpperRightTop();

  m_uiData.focalDistance = 1.0f;
  m_uiData.blurStrength  = 1.0f;
  m_uiData.blurRadius    = 2;
}

void SceneGraphViewerApp::createDepthStencilSRV(const ComPtr<ID3D12Resource>& depthTexture)
{
  // Ensure the descriptor heap exists
  if (!m_depthSrvDescriptorHeap)
  {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors             = 1;
    heapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    throwIfFailed(getDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_depthSrvDescriptorHeap)));
  }

  // Configure the SRV descriptor
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format                          = DXGI_FORMAT_R32_FLOAT;
  srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels             = 1;
  srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  // Create the SRV
  CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_depthSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
  getDevice()->CreateShaderResourceView(depthTexture.Get(), &srvDesc, srvHandle);

  // ** Speichern des GPU Handles für Compute Shader**
  CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle(m_depthSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
  m_depthTexture_GPU_SRV = gpuSrvHandle;
}

void SceneGraphViewerApp::createComputeRootSignature()
{
  CD3DX12_ROOT_PARAMETER parameters[10] = {};

  // **t0 - SRV für Offscreen-Target**
  CD3DX12_DESCRIPTOR_RANGE srvTable0;
  srvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  parameters[0].InitAsDescriptorTable(1, &srvTable0);

  // **u0 - UAV für das Ping-Pong Buffer**
  CD3DX12_DESCRIPTOR_RANGE uavTable1;
  uavTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
  parameters[1].InitAsDescriptorTable(1, &uavTable1);

  // **t1 - SRV für das Ping-Pong Buffer**
  CD3DX12_DESCRIPTOR_RANGE srvTable2;
  srvTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
  parameters[2].InitAsDescriptorTable(1, &srvTable2);

  // **u1 - UAV für das finale Bild**
  CD3DX12_DESCRIPTOR_RANGE uavTable3;
  uavTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
  parameters[3].InitAsDescriptorTable(1, &uavTable3);

  // **t2 - SRV für das Ping-Pong Buffer**
  CD3DX12_DESCRIPTOR_RANGE srvTable4;
  srvTable4.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
  parameters[4].InitAsDescriptorTable(1, &srvTable4);

  // **u2 - UAV für das Ping-Pong Buffer**
  CD3DX12_DESCRIPTOR_RANGE uavTable5;
  uavTable5.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
  parameters[5].InitAsDescriptorTable(1, &uavTable5);

  // **t3 - SRV für finalTarget Buffer**
  CD3DX12_DESCRIPTOR_RANGE srvTable6;
  srvTable6.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
  parameters[6].InitAsDescriptorTable(1, &srvTable6);

  // **u3 - UAV für finalTarget Buffer**
  CD3DX12_DESCRIPTOR_RANGE uavTable7;
  uavTable7.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
  parameters[7].InitAsDescriptorTable(1, &uavTable7);

  // **t3 - SRV für finalTarget Buffer**
  CD3DX12_DESCRIPTOR_RANGE srvTable8;
  srvTable8.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
  parameters[8].InitAsDescriptorTable(1, &srvTable8);

  // **b0 - Constant Buffer**
  parameters[9].InitAsConstantBufferView(0);

  CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
  descRootSignature.Init(_countof(parameters), parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

  ComPtr<ID3DBlob> rootBlob, errorBlob;
  D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);

  getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&m_computeRootSignature));
}

void SceneGraphViewerApp::createComputePipeline()
{
  // 1 **Horizontaler Blur Compute Shader laden**
  const auto horizontalComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/ComputeShader.hlsl", L"HorizontalBlur", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                    = m_computeRootSignature.Get();
  psoDesc.CS                                = HLSLCompiler::convert(horizontalComputeShader);
  psoDesc.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateHorizontal)));

  // 2 **Vertikaler Blur Compute Shader laden**
  const auto verticalComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/ComputeShader.hlsl", L"VerticalBlur", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(verticalComputeShader); // **Nur Shader ändern, RootSignature bleibt gleich!**

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateVertical)));

  // **3 Depth of Field Pipeline**
  const auto dofComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/DepthOfField.hlsl", L"main", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(dofComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateDoF)));
}

void SceneGraphViewerApp::createSRVandUAV()
{
  auto device = getDevice();
  if (!device)
    throw std::runtime_error("Direct3D 12 Device nicht gefunden.");

  // **1 Beschreibung der Ping-Pong Texturen & Final Target**
  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Width               = getWidth();
  textureDesc.Height              = getHeight();
  textureDesc.DepthOrArraySize    = 1;
  textureDesc.MipLevels           = 1;
  textureDesc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
  textureDesc.SampleDesc.Count    = 1;
  textureDesc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
  CD3DX12_CLEAR_VALUE     clearValue(textureDesc.Format, std::array<float, 4> {0.318f, 0.557f, 0.722f, 1.0f}.data());

  // **2 Erstellen der Render- und Ping-Pong-Texturen**
  throwIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                                                IID_PPV_ARGS(&m_offscreenTarget)));

  throwIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &clearValue,
                                                IID_PPV_ARGS(&m_gaussianTarget_x)));

  throwIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &clearValue,
                                                IID_PPV_ARGS(&m_gaussianTarget_y)));

  throwIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &clearValue,
                                                IID_PPV_ARGS(&m_finalTarget)));

  // **3 RTV für das Render-Target (`m_offscreenTarget`) hinzufügen**
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors             = 1;
  rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  throwIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescriptorHeap)));

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
  device->CreateRenderTargetView(m_offscreenTarget.Get(), nullptr, rtvHandle);
  m_offscreenTarget_CPU_RTV = rtvHandle; // **RTV speichern für `OMSetRenderTargets()`**

  // **4 Descriptor Heap für SRV/UAV erstellen**
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  // **1x SRV (t0), 1x UAV (u0), 1x SRV (t1), 1x UAV (u1), 1x SRV (t2), 1x UAV (u2), 1x SRV (t3), 1x SRV (t4)**
  heapDesc.NumDescriptors = 13;
  heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  throwIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));

  CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_descriptorHeap->GetCPUDescriptorHandleForHeapStart());
  CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle(m_descriptorHeap->GetGPUDescriptorHandleForHeapStart());
  UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // **5 SRV für das Originalbild (t0)**
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels             = 1;
  device->CreateShaderResourceView(m_offscreenTarget.Get(), &srvDesc, srvHandle);
  m_offscreenTarget_GPU_SRV = gpuSrvHandle;

  // **6 UAV für Blur - X (u0)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.Format                           = DXGI_FORMAT_R8G8B8A8_UNORM;
  uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
  uavDesc.Texture2D.MipSlice               = 0;
  device->CreateUnorderedAccessView(m_gaussianTarget_x.Get(), nullptr, &uavDesc, srvHandle);
  m_gaussianBlur_X_GPU_UAV = gpuSrvHandle;

  // **7 SRV für Blur - X (t1)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateShaderResourceView(m_gaussianTarget_x.Get(), &srvDesc, srvHandle);
  m_gaussianBlur_X_GPU_SRV = gpuSrvHandle;

  // **8 UAV für Blur - Y (u1)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateUnorderedAccessView(m_gaussianTarget_y.Get(), nullptr, &uavDesc, srvHandle);
  m_gaussianBlur_Y_GPU_UAV = gpuSrvHandle;

  // **9 SRV für Blur - Y (t2)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateShaderResourceView(m_gaussianTarget_y.Get(), &srvDesc, srvHandle);
  m_gaussianBlur_Y_GPU_SRV = gpuSrvHandle;

  // **10 UAV für das finale Ausgabe-Bild (u2)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateUnorderedAccessView(m_finalTarget.Get(), nullptr, &uavDesc, srvHandle);
  m_finalTarget_GPU_UAV = gpuSrvHandle;

  // **11 SRV für das Ping-Pong Buffer (t3)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateShaderResourceView(m_finalTarget.Get(), &srvDesc, srvHandle);
  m_finalTarget_GPU_SRV = gpuSrvHandle;

  // **12 SRV für DepthStencil (t4)**
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  srvDesc.Format                  = DXGI_FORMAT_R32_FLOAT;
  srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels     = 1;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(getDepthStencil().Get(), &srvDesc, srvHandle);
  m_depthTexture_GPU_SRV = gpuSrvHandle;

  // ========== SRV für edgeTable (t5) ==========
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);

  D3D12_SHADER_RESOURCE_VIEW_DESC edgeSrvDesc = {};
  edgeSrvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  edgeSrvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
  edgeSrvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
  edgeSrvDesc.Buffer.FirstElement             = 0;
  edgeSrvDesc.Buffer.NumElements              = 256;
  edgeSrvDesc.Buffer.StructureByteStride      = sizeof(uint32_t);
  edgeSrvDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_NONE;

  device->CreateShaderResourceView(m_EdgeTableBuffer.Get(), &edgeSrvDesc, srvHandle);
  m_edgeTable_GPU_SRV = gpuSrvHandle;

  // ========== SRV für triTable (t6) ==========
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);

  D3D12_SHADER_RESOURCE_VIEW_DESC triSrvDesc = edgeSrvDesc;
  triSrvDesc.Buffer.NumElements              = 256 * 16;
  triSrvDesc.Buffer.StructureByteStride      = sizeof(int);

  device->CreateShaderResourceView(m_TriTableBuffer.Get(), &triSrvDesc, srvHandle);
  m_triTable_GPU_SRV = gpuSrvHandle;

  // == == == == == SRV für GridBufferA(t7) == == == == ==
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);

  D3D12_SHADER_RESOURCE_VIEW_DESC gridSrvDesc = {};
  gridSrvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
  gridSrvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  gridSrvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
  gridSrvDesc.Buffer.FirstElement             = 0;
  gridSrvDesc.Buffer.NumElements              = 640 * 480; // oder NX * NY * NZ
  gridSrvDesc.Buffer.StructureByteStride      = sizeof(LBCell);
  gridSrvDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_NONE;

  device->CreateShaderResourceView(m_GridBufferA.Get(), &gridSrvDesc, srvHandle);
  m_gridBufferA_GPU_SRV = gpuSrvHandle; // zum späteren Binden im Mesh Shader

  // === t8: 3D cellTypeBuffer ===
  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);

  D3D12_SHADER_RESOURCE_VIEW_DESC grid3dSrvDesc = {};
  grid3dSrvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
  grid3dSrvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  grid3dSrvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
  grid3dSrvDesc.Buffer.FirstElement             = 0;
  grid3dSrvDesc.Buffer.NumElements         = m_uiData.gridAdjusterX * m_uiData.gridAdjusterY * m_uiData.gridAdjusterZ;
  grid3dSrvDesc.Buffer.StructureByteStride = sizeof(int);
  grid3dSrvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

  device->CreateShaderResourceView(m_CellTypeBuffer3D.Get(), &grid3dSrvDesc, srvHandle);
  m_cellTypeBufferA3D_GPU_SRV = gpuSrvHandle;



  D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
  uav.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
  uav.Format                     = DXGI_FORMAT_UNKNOWN; // Structured!
  uav.Buffer.FirstElement        = 0;
  uav.Buffer.NumElements         = 1; // ein uint
  uav.Buffer.StructureByteStride = sizeof(UINT);
  uav.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_NONE;


  srvHandle.Offset(1, descriptorSize);
  gpuSrvHandle.Offset(1, descriptorSize);
  device->CreateUnorderedAccessView(m_triCountUAV.Get(), nullptr, &uav, srvHandle);
  m_triCountUAV_GPU_UAV = gpuSrvHandle;

}

void SceneGraphViewerApp::createLBMComputeRootSignature()
{

  // Wir brauchen 3 Parameter:
  //  - Parameter[0]: RWStructuredBuffer an u0 (Root UAV)
  //  - Parameter[1]: RWStructuredBuffer an u1 (Root UAV)
  //  - Parameter[2]: Descriptor Table für typisierte UAV(s) an u2

  CD3DX12_ROOT_PARAMETER parameters[5] = {};

  // 1) RWStructuredBuffer an u0 als Root UAV
  parameters[0].InitAsUnorderedAccessView(0, // ShaderRegister (u0)
                                          0  // RegisterSpace
  );

  // 2) RWStructuredBuffer an u1 als Root UAV
  parameters[1].InitAsUnorderedAccessView(1, // ShaderRegister (u1)
                                          0  // RegisterSpace
  );

  // 3) Descriptor Table für eine (oder mehrere) UAV(s) bei u2
  CD3DX12_DESCRIPTOR_RANGE uavRange;
  uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, // Art des Deskriptor-Bereichs
                1,                               // Anzahl der UAV-Deskriptoren
                2                                // BaseShaderRegister = u2
                // RegisterSpace = 0, Offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND (Standardwerte)
  );
  parameters[2].InitAsDescriptorTable(1,        // Anzahl Descriptor Ranges
                                      &uavRange // Pointer auf das Array von Ranges
  );

  // NEU: b_cellTypes als UAV an u3 (direkt per Root UAV)
  parameters[3].InitAsUnorderedAccessView(3); // u3

  // **b0 - Constant Buffer**
  parameters[4].InitAsConstantBufferView(0);

  // Root-Signature-Descriptor erstellen
  CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
  descRootSignature.Init(_countof(parameters), // Anzahl Root-Parameter
                         parameters,
                         0, // Keine statischen Sampler
                         nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

  ComPtr<ID3DBlob> rootBlob, errorBlob;
  D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);

  getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&m_LBMcomputeRootSignature));
}

void SceneGraphViewerApp::createLBM3DComputeRootSignature()
{

  // Wir brauchen 3 Parameter:
  //  - Parameter[0]: RWStructuredBuffer an u0 (Root UAV)
  //  - Parameter[1]: RWStructuredBuffer an u1 (Root UAV)
  //  - Parameter[2]: Descriptor Table für typisierte UAV(s) an u2

  CD3DX12_ROOT_PARAMETER parameters[5] = {};

  // 1) RWStructuredBuffer an u0 als Root UAV
  parameters[0].InitAsUnorderedAccessView(0, // ShaderRegister (u0)
                                          0  // RegisterSpace
  );

  // 2) RWStructuredBuffer an u1 als Root UAV
  parameters[1].InitAsUnorderedAccessView(1, // ShaderRegister (u1)
                                          0  // RegisterSpace
  );

  // 3) Descriptor Table für eine (oder mehrere) UAV(s) bei u2
  CD3DX12_DESCRIPTOR_RANGE uavRange;
  uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, // Art des Deskriptor-Bereichs
                1,                               // Anzahl der UAV-Deskriptoren
                2                                // BaseShaderRegister = u2
                // RegisterSpace = 0, Offset = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND (Standardwerte)
  );
  parameters[2].InitAsDescriptorTable(1,        // Anzahl Descriptor Ranges
                                      &uavRange // Pointer auf das Array von Ranges
  );

  // NEU: b_cellTypes als UAV an u3 (direkt per Root UAV)
  parameters[3].InitAsUnorderedAccessView(3); // u3

  // **b0 - Constant Buffer**
  parameters[4].InitAsConstantBufferView(0);

  // Root-Signature-Descriptor erstellen
  CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
  descRootSignature.Init(_countof(parameters), // Anzahl Root-Parameter
                         parameters,
                         0, // Keine statischen Sampler
                         nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

  ComPtr<ID3DBlob> rootBlob, errorBlob;
  D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);

  getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&m_LBM3DcomputeRootSignature));
}

void SceneGraphViewerApp::createLBM3DPipeline()
{
  const auto lbmComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/LBM3D.hlsl", L"main", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                    = m_LBM3DcomputeRootSignature.Get();
  psoDesc.CS                                = HLSLCompiler::convert(lbmComputeShader);
  psoDesc.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBM3D)));

  // 2 ** Streaming step **
  const auto lbmStreamComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/LBM3DStreaming.hlsl", L"main", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(lbmStreamComputeShader); // **Nur Shader ändern, RootSignature bleibt gleich!**

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBM3DStream)));

  // 3 ** Cell Type Update Step **
  const auto lbmCellTypeComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/LBM3D_UpdateCellTypes.hlsl", L"main", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(lbmCellTypeComputeShader);

  throwIfFailed(
      getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBM3DUpdateCellTypes)));
}

void SceneGraphViewerApp::createLBMPipeline()
{
  const auto lbmComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/LBM.hlsl", L"main", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                    = m_LBMcomputeRootSignature.Get();
  psoDesc.CS                                = HLSLCompiler::convert(lbmComputeShader);
  psoDesc.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBM)));

  // 2 ** Streaming step **
  const auto lbmStreamComputeShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/LBMStreaming.hlsl", L"main", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(lbmStreamComputeShader); // **Nur Shader ändern, RootSignature bleibt gleich!**

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBMStream)));
}

void SceneGraphViewerApp::CreateLBMBuffers()
{
  auto device = getDevice();
  if (!device)
    throw std::runtime_error("Direct3D 12 Device nicht gefunden.");

  int gridWidth  = 640;
  int gridHeight = 480;

  // Anzahl Zellen = Breite * Höhe (für 2D-Gitter)

  const UINT   cellCount         = gridWidth * gridHeight;
  const UINT64 bufferSizeInBytes = static_cast<UINT64>(cellCount) * sizeof(LBCell);

  const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  const auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                       IID_PPV_ARGS(m_GridBufferA.GetAddressOf()));

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                       IID_PPV_ARGS(m_GridBufferB.GetAddressOf()));

  // ===== BUFFER FUER ZELLTYPES =====
  const auto cellBufferSizeInBytes = static_cast<UINT64>(cellCount) * sizeof(ui32);

  const auto cellBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cellBufferSizeInBytes);

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &cellBufferDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(m_CellTypeBuffer.GetAddressOf()));
}

void SceneGraphViewerApp::CreateLBM3DBuffers()
{
  auto device = getDevice();
  if (!device)
    throw std::runtime_error("Direct3D 12 Device nicht gefunden.");

  // int gridWidth  = 128;
  int gridWidth  = m_uiData.gridAdjusterX;
  int gridHeight = m_uiData.gridAdjusterY;
  int gridDepth  = m_uiData.gridAdjusterZ;

  // Anzahl Zellen = Breite * Höhe * Tiefe (für 3D-Gitter)

  const UINT   cellCount         = gridWidth * gridHeight * gridDepth;
  const UINT64 bufferSizeInBytes = static_cast<UINT64>(cellCount) * sizeof(LBCell3D);

  const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  const auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                       IID_PPV_ARGS(m_GridBufferA3D.GetAddressOf()));

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                       IID_PPV_ARGS(m_GridBufferB3D.GetAddressOf()));

  // ===== BUFFER FUER ZELLTYPES =====
  const auto cellBufferSizeInBytes = static_cast<UINT64>(cellCount) * sizeof(ui32);

  const auto cellBufferDesc =
      CD3DX12_RESOURCE_DESC::Buffer(cellBufferSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &cellBufferDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(m_CellTypeBuffer3D.GetAddressOf()));
}

void SceneGraphViewerApp::InitializeLBMGridWithStreifen()
{
  int gridWidth  = 640;
  int gridHeight = 480;

  const UINT cellCount = gridWidth * gridHeight;

  const gims::f32v2 c[9] = {
      {0.0f, 0.0f},   // Ruhe
      {1.0f, 0.0f},   // rechts
      {-1.0f, 0.0f},  // links
      {0.0f, 1.0f},   // oben
      {0.0f, -1.0f},  // unten
      {1.0f, 1.0f},   // oben rechts
      {-1.0f, -1.0f}, // unten links
      {-1.0f, 1.0f},  // oben links
      {1.0f, -1.0f}   // unten rechts
  };

  const float w[9] = {4.0f / 9.0f, // i = 0
                      1.0f / 9.0f, // i = 1,2,3,4
                      1.0f / 9.0f,  1.0f / 9.0f,  1.0f / 9.0f,
                      1.0f / 36.0f, // i = 5,6,7,8
                      1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f};

  std::vector<LBCell>   initialGrid(cellCount);
  std::vector<uint32_t> cellTypes(cellCount, 0); // 0 = Fluid

  // OBSTACLE DEFINITION
  int centerX = gridWidth / 4;
  int centerY = gridHeight / 2;
  int radius  = gridHeight / 10;

  for (int y = 0; y < gridHeight; ++y)
  {
    for (int x = 0; x < gridWidth; ++x)
    {
      int   idx  = y * gridWidth + x;
      auto& cell = initialGrid[idx];

      float dx     = x - (float)centerX;
      float dy     = y - (float)centerY;
      float distSq = dx * dx + dy * dy;

      // === Zelltyp bestimmen ===
      if (distSq <= radius * radius)
      {
        cellTypes[idx] = 3; // OBSTACLE
      }

      //    = WALL 4 =
      else if (y == 0 || y == gridHeight - 1)
      {
        cellTypes[idx] = 4; // Cell_Wall
      }

      //    = OUTFLOW 2 =
      else if (x == gridWidth - 1)
      {
        cellTypes[idx] = 2; // Cell_OUTFLOW
      }

      //    = INFLOW 1 =
      else if (x == 0)
      {
        cellTypes[idx] = 1; // Cell_Inflow
        // std::cout << "Set inflow at: " << idx << std::endl;
      }

      else
      {
        cellTypes[idx] = 0; // FLUID
      }

      cell.rho = 1.0f;

      if (cellTypes[idx] == 4 || cellTypes[idx] == 3 /* Wall oder Obstacle*/)
      {
        // Keine Geschwindigkeit, keine Bewegung
        cell.u = {0.0f, 0.0f};
        for (int i = 0; i < 9; ++i)
          // cell.f[i] = 0.0f; // oder: feq mit u = 0
          cell.f[i] = w[i];
      }

      else if (cellTypes[idx] == 2 /* OUTFLOW */)
      {
        // Keine Geschwindigkeit, keine Bewegung
        cell.u   = {0.208f, 0.0f};
        cell.rho = 1.0f;
        for (int i = 0; i < 9; ++i)
        {
          const auto& ci = c[i];
          float       cu = ci.x * cell.u.x + ci.y * cell.u.y;
          float       u2 = dot(cell.u, cell.u);
          cell.f[i]      = w[i] * cell.rho * (1 + 3 * cu + 4.5f * cu * cu - 1.5f * u2);
        }
      }
      else if (cellTypes[idx] == 1 /* INFLOW */)
      {
        // Keine Geschwindigkeit, keine Bewegung
        /*
          cell.u   = {0.1f, 0.0f};
        cell.rho = 1.0f;
        for (int i = 0; i < 9; ++i)
        {
          float cu  = c[i].x * cell.u.x + c[i].y * cell.u.y;
          cell.f[i] = w[i] * cell.rho *
                      (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * (cell.u.x * cell.u.x + cell.u.y * cell.u.y));
        }
        */

        {
          cell.rho = 1.0f;
          cell.u   = {0.0f, 0.0f};

          // Gleichgewichtsverteilung f[i]
          for (int i = 0; i < 9; ++i)
          {
            const auto& ci = c[i];
            float       cu = ci.x * cell.u.x + ci.y * cell.u.y;
            float       u2 = dot(cell.u, cell.u);
            cell.f[i]      = w[i] * cell.rho * (1 + 3 * cu + 4.5f * cu * cu - 1.5f * u2);
          }
        }
      }

      else
      {

        cell.u = {0.0f, 0.0f};

        // Gleichgewichtsverteilung f[i]
        for (int i = 0; i < 9; ++i)
        {
          const auto& ci = c[i];
          float       cu = ci.x * cell.u.x + ci.y * cell.u.y;
          float       u2 = dot(cell.u, cell.u);
          cell.f[i]      = w[i] * cell.rho * (1 + 3 * cu + 4.5f * cu * cu - 1.5f * u2);
        }
      }
    }
  }

  // GridBuffer
  const size_t gridBufferSize = cellCount * sizeof(LBCell);
  UploadHelper gridUpload(getDevice(), gridBufferSize);
  gridUpload.uploadDefaultBuffer(initialGrid.data(), m_GridBufferA, gridBufferSize, getCommandQueue());

  // CellTypeBuffer
  const size_t typeBufferSize = cellCount * sizeof(uint32_t);
  UploadHelper typeUpload(getDevice(), typeBufferSize);
  typeUpload.uploadDefaultBuffer(cellTypes.data(), m_CellTypeBuffer, typeBufferSize, getCommandQueue());
}

void SceneGraphViewerApp::InitializeLBMGrid3D()
{
  int gridWidth  = m_uiData.gridAdjusterX;
  int gridHeight = m_uiData.gridAdjusterY;
  int gridDepth  = m_uiData.gridAdjusterZ;

  const UINT cellCount = gridWidth * gridHeight * gridDepth;

  const gims::f32v3 c[19] = {{0, 0, 0},   {1, 0, 0},  {-1, 0, 0}, {0, 1, 0},   {0, -1, 0}, {0, 0, 1},  {0, 0, -1},
                             {1, 1, 0},   {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1}, {1, 0, -1},
                             {-1, 0, -1}, {0, 1, 1},  {0, -1, 1}, {0, 1, -1},  {0, -1, -1}};

  const float w[19] = {1.0f / 3.0f,  1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                       1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f,
                       1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f};

  std::vector<LBCell3D> initialGrid(cellCount);
  std::vector<uint32_t> cellTypes(cellCount, 0);

  const float       rhoInit = 0.0f;
  const float       mInit   = 0.0f;
  const gims::f32v3 uInit   = {0.0f, 0.0f, 0.0f};
  // const float       rhoInflow      = 1.0f;
  // const float       mInflow        = 1.0f;
  const gims::f32v3 inflowVelocity = {0.0f, 0.01f, 0.0f};

  for (int z = 0; z < gridDepth; ++z)
  {
    for (int y = 0; y < gridHeight; ++y)
    {
      for (int x = 0; x < gridWidth; ++x)
      {
        int   idx  = x + y * gridWidth + z * gridWidth * gridHeight;
        auto& cell = initialGrid[idx];

        // Treppenstruktur entlang X (massiv unterhalb Stufe)
        int  stairHeight = x / 4; // Steigung: 1:4 (anpassbar)
        bool isStair     = (y <= stairHeight && y > 0 && x > 0 && x < gridWidth);

        bool isCube = (x >= 50 && x < 100 && y >= 5 && y < 50 && z >= 30 && z < 70);

        bool isWall = isStair || isCube;
        if (y == 0)
        {
          isWall = false;
        }
        
        if (isWall)
        {
          cellTypes[idx] = CELL_WALL;
          cell.rho       = rhoInit;
          cell.u         = {0.0f, 0.0f, 0.0f};
          cell.m         = mInit;
          cell.epsilon   = 0.0f;

          for (int i = 0; i < 19; ++i)
            cell.f[i] = w[i] * cell.rho;
        }

        else
        {
          // Hauptkugel (mittig)
          const gims::f32v3 centerMain = {gridWidth * 0.5f, gridHeight * 0.5f , gridDepth * 0.5f};
          const float       radiusMain = 10.0f;

          // Zweite Kugel (oben links als Inflow)
          const gims::f32v3 centerInflow = {8.0f, gridHeight - 8.0f, 8.0f};
          // const float       radiusInflow = 6.0f;

          gims::f32v3 pos    = {float(x), float(y), float(z)};
          auto        distSq = [](gims::f32v3 a, gims::f32v3 b)
          { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z); };

          
          if (distSq(pos, centerMain) <= radiusMain * radiusMain)
          {
            // Hauptkugel
            cellTypes[idx] = CELL_INFLOW;
            cell.rho       = 0.001f;
            cell.u         = uInit;
            cell.m         = 0.10f;
            cell.epsilon   = 1.0f;

            for (int i = 0; i < 19; ++i)
            {
              float cu  = dot(c[i], cell.u);
              float u2  = dot(cell.u, cell.u);
              cell.f[i] = w[i] * cell.rho * (1 + 3 * cu + 4.5f * cu * cu - 1.5f * u2);
            }
          }

          else
          {
            // Rest: leer
            cellTypes[idx] = CELL_EMPTY;
            cell.rho       = 0.0f;
            cell.u         = {0.0f, 0.0f, 0.0f};
            cell.m         = 0.0f;
            cell.epsilon   = 0.0f;

            for (int i = 0; i < 19; ++i)
              cell.f[i] = 0.0f;
          }
        }
      }
    }
  }
  

  const size_t gridBufferSize = cellCount * sizeof(LBCell3D);
  UploadHelper gridUpload(getDevice(), gridBufferSize);
  gridUpload.uploadDefaultBuffer(initialGrid.data(), m_GridBufferA3D, gridBufferSize, getCommandQueue());

  const size_t typeBufferSize = cellCount * sizeof(uint32_t);
  UploadHelper typeUpload(getDevice(), typeBufferSize);
  typeUpload.uploadDefaultBuffer(cellTypes.data(), m_CellTypeBuffer3D, typeBufferSize, getCommandQueue());
}



void SceneGraphViewerApp::InitializeLBMGrid3D_v2() 

{
  // Grid aus UI
  int gridWidth  = m_uiData.gridAdjusterX;
  int gridHeight = m_uiData.gridAdjusterY;
  int gridDepth  = m_uiData.gridAdjusterZ;

  const UINT cellCount = gridWidth * gridHeight * gridDepth;

  // D3Q19 Geschwindigkeiten & Gewichte
  const gims::f32v3 c[19] = {{0, 0, 0},   {1, 0, 0},  {-1, 0, 0}, {0, 1, 0},   {0, -1, 0}, {0, 0, 1},  {0, 0, -1},
                             {1, 1, 0},   {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1}, {1, 0, -1},
                             {-1, 0, -1}, {0, 1, 1},  {0, -1, 1}, {0, 1, -1},  {0, -1, -1}};

  const float w[19] = {1.0f / 3.0f,  1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f, 1.0f / 18.0f,
                       1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f,
                       1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f, 1.0f / 36.0f};

  std::vector<LBCell3D> initialGrid(cellCount);
  std::vector<uint32_t> cellTypes(cellCount, 0);

  // Neutrale Fluid-Initialisierung
  const float       rhoFluid = 1.0f;
  const float       mFluid   = 0.10f;
  const gims::f32v3 uZero    = {0.0f, 0.0f, 0.0f};

  // Abfluss (Outflow) in der Mitte des Bodens
  const gims::f32v3 drainCenter = {gridWidth * 0.5f, 0.0f, gridDepth * 0.5f};
  const float       drainRadius = std::max(3.0f, 0.10f * std::min((float)gridWidth, (float)gridDepth));

  auto distSq = [](const gims::f32v3& a, const gims::f32v3& b)
  { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z); };

  auto feq = [&](float rho, const gims::f32v3& u, int i)
  {
    float cu = c[i].x * u.x + c[i].y * u.y + c[i].z * u.z;
    float u2 = u.x * u.x + u.y * u.y + u.z * u.z;
    return w[i] * rho * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * u2);
  };

  for (int z = 0; z < gridDepth; ++z)
  {
    for (int y = 0; y < gridHeight; ++y)
    {
      for (int x = 0; x < gridWidth; ++x)
      {
        const int idx  = x + y * gridWidth + z * gridWidth * gridHeight;
        auto&     cell = initialGrid[idx];

        // 1) Default: alles Fluid
        cellTypes[idx] = CELL_FLUID;
        cell.rho       = rhoFluid;
        cell.u         = uZero;
        cell.m         = mFluid;
        cell.epsilon   = 1.0f;
        for (int i = 0; i < 19; ++i)
          cell.f[i] = feq(cell.rho, cell.u, i);

        // 2) Seitenwände + Decke als WALL
        bool isSideOrCeilWall =
            (x == 0) || (x == gridWidth - 1) || (z == 0) || (z == gridDepth - 1) || (y == gridHeight - 1);

        // 3) Boden: Outflow-Kreis vs. Wand
        bool isFloor = (y == 0);
        bool isDrain = false;
        if (isFloor)
        {
          gims::f32v3 p  = {(float)x, 0.0f, (float)z};
          gims::f32v3 dc = {drainCenter.x, 0.0f, drainCenter.z};
          isDrain        = (distSq(p, dc) <= drainRadius * drainRadius);
        }

        // Priorität: Boden-Drain (OUTFLOW) vor Boden-WALL, Seiten/Decke immer WALL
        if (isSideOrCeilWall || (isFloor && !isDrain))
        {
          cellTypes[idx] = CELL_WALL;
          cell.rho       = 0.0f;
          cell.u         = {0.0f, 0.0f, 0.0f};
          cell.m         = 0.0f;
          cell.epsilon   = 0.0f;
          for (int i = 0; i < 19; ++i)
            cell.f[i] = w[i] * cell.rho;
        }
        else if (isDrain)
        {
          cellTypes[idx] = CELL_OUTFLOW;
          // neutrale Startwerte; Abfluss-Regel erfolgt im Shader
          cell.rho     = rhoFluid; // typ. 1.0
          cell.u       = uZero;
          cell.m       = 0.0f; // optional 0 für Rand
          cell.epsilon = 1.0f;
          for (int i = 0; i < 19; ++i)
            cell.f[i] = feq(cell.rho, cell.u, i);
        }
      }
    }
  }

  // Upload
  const size_t gridBufferSize = cellCount * sizeof(LBCell3D);
  UploadHelper gridUpload(getDevice(), gridBufferSize);
  gridUpload.uploadDefaultBuffer(initialGrid.data(), m_GridBufferA3D, gridBufferSize, getCommandQueue());

  const size_t typeBufferSize = cellCount * sizeof(uint32_t);
  UploadHelper typeUpload(getDevice(), typeBufferSize);
  typeUpload.uploadDefaultBuffer(cellTypes.data(), m_CellTypeBuffer3D, typeBufferSize, getCommandQueue());
}


void SceneGraphViewerApp::createMeshRootSignature()
{
  // Root Parameter 0: Constant Buffer View (z. B. MVP)
  // Root Parameter 1: 32-Bit Shader Constants (z. B. Einstellungen)
  // Root Parameter 2: Descriptor Table mit 2 SRVs (t0, t1)
  CD3DX12_ROOT_PARAMETER parameters[5];
  parameters[0].InitAsConstantBufferView(0); // b0
  // Root Parameter 1: 32-Bit Constant
  parameters[1].InitAsConstants(16, 1); // Register b1 z.B.
  // Noch keine Descriptor Tables oder Sampler nötig

  // SRV-Descriptor-Table für edgeTable (t0) und triTable (t1)
  CD3DX12_DESCRIPTOR_RANGE srvRange;
  srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0); // 2 SRVs ab t0

  parameters[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

  // Root Parameter 3: Descriptor Table with LBM Data for grid[t2]
  CD3DX12_DESCRIPTOR_RANGE srvRangeGrid;
  srvRangeGrid.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // 1 SRV ab t2

  parameters[3].InitAsDescriptorTable(1, &srvRangeGrid, D3D12_SHADER_VISIBILITY_ALL);

  // NEU: UAV u5 (ein Descriptor)
  CD3DX12_DESCRIPTOR_RANGE uavRange;
  uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5); // 1 UAV ab u5

  parameters[4].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_MESH);





  CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
  descRootSignature.Init(_countof(parameters), parameters,
                         0, // Keine statischen Sampler
                         nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);


  ComPtr<ID3DBlob> serializedRootSig, errorBlob;
  HRESULT          hr =
      D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);

  if (FAILED(hr))
  {
    if (errorBlob)
    {
      OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    throw std::runtime_error("Mesh Root Signature serialization failed.");
  }

  throwIfFailed(getDevice()->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
                                                 serializedRootSig->GetBufferSize(),
                                                 IID_PPV_ARGS(&m_meshRootSignature)));
}

void SceneGraphViewerApp::createMeshPipeline()
{

  // 1. Shader kompilieren
  const auto meshShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/MarchingLBM.hlsl", L"main", L"ms_6_5");
  const auto pixelShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/MarchingLBM.hlsl", L"PSMain", L"ps_6_5");

  D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                         = m_meshRootSignature.Get();
  psoDesc.MS                                     = HLSLCompiler::convert(meshShader);
  psoDesc.PS                                     = HLSLCompiler::convert(pixelShader);
  psoDesc.RasterizerState                        = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState                             = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DSVFormat                              = getDX12AppConfig().depthBufferFormat;
  psoDesc.DepthStencilState                      = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable          = TRUE;
  psoDesc.DepthStencilState.StencilEnable        = TRUE;
  psoDesc.SampleMask                             = UINT_MAX;
  psoDesc.NumRenderTargets                       = 1;
  psoDesc.RTVFormats[0]                          = getRenderTarget()->GetDesc().Format;
  psoDesc.DSVFormat                              = getDepthStencil()->GetDesc().Format;
  psoDesc.SampleDesc.Count                       = 1;

  psoDesc.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

  auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);

  D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
  streamDesc.pPipelineStateSubobjectStream = &psoStream;
  streamDesc.SizeInBytes                   = sizeof(psoStream);

  // 7. ID3D12Device2 besorgen
  ComPtr<ID3D12Device2> device2;
  throwIfFailed(getDevice()->QueryInterface(IID_PPV_ARGS(&device2)));

  throwIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshPipelineState)));
}

void SceneGraphViewerApp::createDebugMeshPipeline()
{
  // 1. Shader kompilieren
  const auto meshShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/MarchingLBM.hlsl", L"main_debug", L"ms_6_5");
  const auto pixelShader =
      compileShader(L"../../../Assignments/V3MarchingCubes/Shaders/MarchingLBM.hlsl", L"PSMain", L"ps_6_5");

  // 2. Shader-Bytecode extrahieren
  D3D12_SHADER_BYTECODE meshShaderBytecode  = HLSLCompiler::convert(meshShader);
  D3D12_SHADER_BYTECODE pixelShaderBytecode = HLSLCompiler::convert(pixelShader);

  // 3. Root Signature vorbereiten
  ID3D12RootSignature* rootSig = m_meshRootSignature.Get();

  // 4. Subobjekte definieren
  struct RootSignatureSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    ID3D12RootSignature*                pRootSignature;
  } rootSignatureSubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, rootSig};

  struct MeshShaderSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    D3D12_SHADER_BYTECODE               Shader;
  } meshShaderSubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, meshShaderBytecode};

  struct PixelShaderSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    D3D12_SHADER_BYTECODE               Shader;
  } pixelShaderSubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, pixelShaderBytecode};

  struct TopologySubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE       TopologyType;
  } topologySubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE};

  struct RTVFormatsSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    D3D12_RT_FORMAT_ARRAY               RTVFormats;
  };

  RTVFormatsSubobject rtvFormatsSubobject         = {};
  rtvFormatsSubobject.Type                        = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
  rtvFormatsSubobject.RTVFormats.NumRenderTargets = 1;
  rtvFormatsSubobject.RTVFormats.RTFormats[0]     = DXGI_FORMAT_R8G8B8A8_UNORM;

  struct DSVFormatSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    DXGI_FORMAT                         Format;
  } dsvFormatSubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT_D24_UNORM_S8_UINT};

  struct SampleDescSubobject
  {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type;
    DXGI_SAMPLE_DESC                    SampleDesc;
  } sampleDescSubobject = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, {1, 0}};

  // 5. Subobjekte in einen Stream schreiben
  uint8_t streamData[sizeof(rootSignatureSubobject) + sizeof(meshShaderSubobject) + sizeof(pixelShaderSubobject) +
                     sizeof(topologySubobject) + sizeof(rtvFormatsSubobject) + sizeof(dsvFormatSubobject) +
                     sizeof(sampleDescSubobject)];

  uint8_t* ptr = streamData;
  memcpy(ptr, &rootSignatureSubobject, sizeof(rootSignatureSubobject));
  ptr += sizeof(rootSignatureSubobject);
  memcpy(ptr, &meshShaderSubobject, sizeof(meshShaderSubobject));
  ptr += sizeof(meshShaderSubobject);
  memcpy(ptr, &pixelShaderSubobject, sizeof(pixelShaderSubobject));
  ptr += sizeof(pixelShaderSubobject);
  memcpy(ptr, &topologySubobject, sizeof(topologySubobject));
  ptr += sizeof(topologySubobject);
  memcpy(ptr, &rtvFormatsSubobject, sizeof(rtvFormatsSubobject));
  ptr += sizeof(rtvFormatsSubobject);
  memcpy(ptr, &dsvFormatSubobject, sizeof(dsvFormatSubobject));
  ptr += sizeof(dsvFormatSubobject);
  memcpy(ptr, &sampleDescSubobject, sizeof(sampleDescSubobject));
  ptr += sizeof(sampleDescSubobject);

  // 6. PSO-Stream-Descriptor definieren
  D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
  streamDesc.pPipelineStateSubobjectStream    = streamData;
  streamDesc.SizeInBytes                      = sizeof(streamData);

  // 7. ID3D12Device2 besorgen
  ComPtr<ID3D12Device2> device2;
  throwIfFailed(getDevice()->QueryInterface(IID_PPV_ARGS(&device2)));

  // 8. Pipeline State erstellen
  throwIfFailed(device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshDebugPipelineState)));
}

void SceneGraphViewerApp::checkMeshShaderSupport()
{
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  HRESULT hr = getDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));

  if (FAILED(hr))
  {
    throw std::runtime_error("D3D12_FEATURE_D3D12_OPTIONS7 not supported (too old runtime/driver).");
  }

  if (options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
  {
    throw std::runtime_error("Mesh Shader not supported on this hardware or driver.");
  }

  // Optional: Infoausgabe
  printf("Mesh Shader Tier supported.\n");
}

void SceneGraphViewerApp::createMarchingCubesLookupBuffers()
{
  auto device = getDevice();
  if (!device)
    throw std::runtime_error("Direct3D 12 Device nicht gefunden.");

  // ========== edgeTableBuffer ==========
  const UINT   edgeTableElementCount = 256;
  const UINT64 edgeTableSizeInBytes  = static_cast<UINT64>(edgeTableElementCount) * sizeof(uint32_t);

  const auto edgeBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(edgeTableSizeInBytes);
  const auto defaultHeap    = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

  device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &edgeBufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                  nullptr, IID_PPV_ARGS(m_EdgeTableBuffer.GetAddressOf()));

  // ========== triTableBuffer ==========
  const UINT   triTableElementCount = 256 * 16;
  const UINT64 triTableSizeInBytes  = static_cast<UINT64>(triTableElementCount) * sizeof(int);

  const auto triBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(triTableSizeInBytes);

  device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &triBufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                  nullptr, IID_PPV_ARGS(m_TriTableBuffer.GetAddressOf()));
}

void SceneGraphViewerApp::uploadMarchingCubesLookupTables()
{
  // === Daten vorbereiten ===
  std::vector<uint32_t> edgeTableData(std::begin(edgeTable), std::end(edgeTable));

  std::vector<int> triTableFlat;
  triTableFlat.reserve(256 * 16);
  for (int i = 0; i < 256; ++i)
  {
    for (int j = 0; j < 16; ++j)
    {
      triTableFlat.push_back(triTable[i][j]);
    }
  }

  // === Upload edgeTable ===
  const size_t edgeTableSize = edgeTableData.size() * sizeof(uint32_t);
  UploadHelper edgeUpload(getDevice(), edgeTableSize);
  edgeUpload.uploadDefaultBuffer(edgeTableData.data(), m_EdgeTableBuffer, edgeTableSize, getCommandQueue());

  // === Upload triTable ===
  const size_t triTableSize = triTableFlat.size() * sizeof(int);
  UploadHelper triUpload(getDevice(), triTableSize);
  triUpload.uploadDefaultBuffer(triTableFlat.data(), m_TriTableBuffer, triTableSize, getCommandQueue());
}

void SceneGraphViewerApp::createDebugQuadBuffers(UINT vertexBufferSize, UINT indexBufferSize)
{
  auto device = getDevice();
  if (!device)
    throw std::runtime_error("Direct3D 12 Device nicht verfügbar");

  const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

  // Vertex Buffer
  const auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
  device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&m_DebugVB));

  // Index Buffer
  const auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
  device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&m_DebugIB));
}

void SceneGraphViewerApp::createDebugQuad()
{
  // Gridgröße passend zur LBM-Domain
  const int gridWidth  = m_uiData.gridAdjusterX;
  const int gridHeight = m_uiData.gridAdjusterY;

  struct DebugVertex
  {
    gims::f32v3 Position;
    gims::f32v2 TexCoord;
  };

  std::vector<DebugVertex> vertices;
  std::vector<uint32_t>    indices;

  // Vertex-Grid erzeugen
  for (int y = 0; y < gridHeight; ++y)
  {
    for (int x = 0; x < gridWidth; ++x)
    {
      DebugVertex v;
      v.Position = gims::f32v3((float)x, (float)y, 0.0f); // Z wird im Shader ersetzt
      v.TexCoord = gims::f32v2(static_cast<float>(x) / (gridWidth - 1), static_cast<float>(y) / (gridHeight - 1));
      vertices.push_back(v);
    }
  }

  // Index-Buffer (zwei Dreiecke pro Quad)
  for (int y = 0; y < gridHeight - 1; ++y)
  {
    for (int x = 0; x < gridWidth - 1; ++x)
    {
      int i = x + y * gridWidth;

      indices.push_back(i);
      indices.push_back(i + 1);
      indices.push_back(i + gridWidth);

      indices.push_back(i + 1);
      indices.push_back(i + gridWidth + 1);
      indices.push_back(i + gridWidth);
    }
  }

  // Größe berechnen
  const UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(DebugVertex));
  const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));

  createDebugQuadBuffers(vbSize, ibSize);

  // Upload durchführen
  UploadHelper uploader(getDevice(), vbSize + ibSize);
  uploader.uploadDefaultBuffer(vertices.data(), m_DebugVB, vbSize, getCommandQueue());
  uploader.uploadDefaultBuffer(indices.data(), m_DebugIB, ibSize, getCommandQueue());

  // Buffer Views setzen
  m_DebugVBView.BufferLocation = m_DebugVB->GetGPUVirtualAddress();
  m_DebugVBView.StrideInBytes  = sizeof(DebugVertex);
  m_DebugVBView.SizeInBytes    = vbSize;

  m_DebugIBView.BufferLocation = m_DebugIB->GetGPUVirtualAddress();
  m_DebugIBView.Format         = DXGI_FORMAT_R32_UINT;
  m_DebugIBView.SizeInBytes    = ibSize;

  m_DebugIndexCount = static_cast<UINT>(indices.size());
}

void SceneGraphViewerApp::createGpuTimingResources()
{
  // 1) Frequenz
  getCommandQueue()->GetTimestampFrequency(&m_tsFrequency);
  m_timestampToMs = (m_tsFrequency > 0) ? (1000.0 / double(m_tsFrequency)) : 0.0;

  // 2) Query-Heap
  D3D12_QUERY_HEAP_DESC qh {};
  qh.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  qh.Count = kFramesInFlight * kStampsMaxPerFrame;
  getDevice()->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_tsHeap));

  // 3) Readback-Buffer
  const UINT64 sizeBytes = UINT64(qh.Count) * sizeof(UINT64);
  auto         props     = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
  auto         desc      = CD3DX12_RESOURCE_DESC::Buffer(sizeBytes);
  getDevice()->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                       IID_PPV_ARGS(&m_tsReadback));

  // Init Mapping
  for (UINT f = 0; f < kFramesInFlight; ++f)
  {
    m_tsCursor[f] = 0u;
    for (UINT m = 0; m < static_cast<UINT>(GpuMark::Count); ++m)
      m_markToIndex[f][m] = UINT_MAX;
  }
}

inline void SceneGraphViewerApp::BeginGpuTiming(UINT frameIdx)
{
  m_tsCursor[frameIdx] = 0u;
  for (UINT m = 0; m < static_cast<UINT>(GpuMark::Count); ++m)
    m_markToIndex[frameIdx][m] = UINT_MAX;
}

inline void SceneGraphViewerApp::Stamp(ID3D12GraphicsCommandList* cl, UINT frameIdx, GpuMark mark)
{
  // Guard: nicht überlaufen
  if (m_tsCursor[frameIdx] >= kStampsMaxPerFrame)
    return;

  const UINT cursor = m_tsCursor[frameIdx]++;
  const UINT idx    = stampBase(frameIdx) + cursor;

  cl->EndQuery(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
  m_markToIndex[frameIdx][static_cast<UINT>(mark)] = idx;
}

inline void SceneGraphViewerApp::EndGpuTiming(ID3D12GraphicsCommandList* cl, UINT frameIdx)
{
  const UINT first = stampBase(frameIdx);
  const UINT count = m_tsCursor[frameIdx];
  if (count == 0)
    return;

  cl->ResolveQueryData(m_tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count, m_tsReadback.Get(),
                       UINT64(first) * sizeof(UINT64));
}

// Lies IMMER das vorige Frame (Ringpuffer), nachdem dessen Fence signalisiert hat.
void SceneGraphViewerApp::collectGpuTimes()
{
  const UINT prev = (getFrameIndex() + kFramesInFlight - 1) % kFramesInFlight;

  const UINT first = stampBase(prev);
  const UINT count = m_tsCursor[prev]; // <-- nur so viele Timestamps sind valide
  if (count == 0)
    return;

  D3D12_RANGE r {};
  r.Begin = UINT64(first) * sizeof(UINT64);
  r.End   = UINT64(first + count) * sizeof(UINT64);

  void* ptr = nullptr;
  if (FAILED(m_tsReadback->Map(0, &r, &ptr)))
    return;

  const UINT64* ticks = reinterpret_cast<const UINT64*>(ptr);

  auto hasTick = [&](GpuMark m, UINT64& out) -> bool
  {
    const UINT idx = m_markToIndex[prev][static_cast<UINT>(m)];
    if (idx == UINT_MAX)
      return false; // in diesem Frame nicht gestempelt
    out = ticks[idx];
    return true;
  };

  auto dtMs = [&](GpuMark a, GpuMark b, double& out) -> bool
  {
    UINT64 ta, tb;
    if (!hasTick(a, ta) || !hasTick(b, tb))
      return false;
    out = double(tb - ta) * m_timestampToMs;
    return true;
  };

  // Beispiele (nur berechnen, wenn beide Marks existieren)
  (void)dtMs(GpuMark::Frame_Begin, GpuMark::Scene_Draw_End, m_gpuMs[static_cast<UINT>(GpuMark::Scene_Draw_End)]);
  (void)dtMs(GpuMark::Scene_Draw_End, GpuMark::Offscreen_To_UAV_End,
             m_gpuMs[static_cast<UINT>(GpuMark::Offscreen_To_UAV_End)]);

  (void)dtMs(GpuMark::PostFog_Begin, GpuMark::PostFog_Mesh_End, m_gpuMs[static_cast<UINT>(GpuMark::PostFog_Mesh_End)]);
  (void)dtMs(GpuMark::PostFog_Mesh_End, GpuMark::PostFog_Debug_End,
             m_gpuMs[static_cast<UINT>(GpuMark::PostFog_Debug_End)]);

  (void)dtMs(GpuMark::LBM2D_Begin, GpuMark::LBM2D_Collision_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM2D_Collision_End)]);
  (void)dtMs(GpuMark::LBM2D_Collision_End, GpuMark::LBM2D_Stream_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM2D_Stream_End)]);
  (void)dtMs(GpuMark::LBM2D_Stream_End, GpuMark::LBM2D_CopyToBackbuffer_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM2D_CopyToBackbuffer_End)]);

  (void)dtMs(GpuMark::LBM3D_Begin, GpuMark::LBM3D_Collision_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM3D_Collision_End)]);
  (void)dtMs(GpuMark::LBM3D_Collision_End, GpuMark::LBM3D_Stream_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM3D_Stream_End)]);
  (void)dtMs(GpuMark::LBM3D_Stream_End, GpuMark::LBM3D_UpdateCellTypes_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM3D_UpdateCellTypes_End)]);
  (void)dtMs(GpuMark::LBM3D_UpdateCellTypes_End, GpuMark::LBM3D_CopyToBackbuffer_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBM3D_CopyToBackbuffer_End)]);

  (void)dtMs(GpuMark::LBMMC_Begin, GpuMark::LBMMC_Dispatch_End,
             m_gpuMs[static_cast<UINT>(GpuMark::LBMMC_Dispatch_End)]);

  (void)dtMs(GpuMark::DebugPlane_Begin, GpuMark::DebugPlane_Draw_End,
             m_gpuMs[static_cast<UINT>(GpuMark::DebugPlane_Draw_End)]);

  // komplette Framezeit:
  (void)dtMs(GpuMark::Frame_Begin, GpuMark::Frame_End, m_gpuMs[static_cast<UINT>(GpuMark::Frame_End)]);

  D3D12_RANGE w {0, 0};
  m_tsReadback->Unmap(0, &w);

  {
    auto get = [&](GpuMark m) -> double { return m_gpuMs[static_cast<UINT>(m)]; };
    // LBM gesamt (ms)
    double t_collision = get(GpuMark::LBM3D_Collision_End);
    double t_stream    = get(GpuMark::LBM3D_Stream_End);
    double t_lbm_ms    = (std::isnan(t_collision) || std::isnan(t_stream)) ? std::numeric_limits<double>::quiet_NaN()
                                                                           : (t_collision + t_stream);

    if (!std::isnan(t_lbm_ms) && t_lbm_ms > 0.0)
    {
      const double cells =
          double(m_uiData.gridAdjusterX) * double(m_uiData.gridAdjusterY) * double(m_uiData.gridAdjusterZ);
      // MLUPS = (Zellen / 1e6) * (1000 / t_ms)
      m_mlups = (cells / 1e6) * (1000.0 / t_lbm_ms);
    }
    else
    {
      m_mlups = std::numeric_limits<double>::quiet_NaN();
    }
  }
}

void SceneGraphViewerApp::queryVram()
{
  ComPtr<IDXGIFactory6> factory;
  CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

  LUID luid = getDevice()->GetAdapterLuid();

  ComPtr<IDXGIAdapter1> adapter1;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter1) != DXGI_ERROR_NOT_FOUND; ++i)
  {
    DXGI_ADAPTER_DESC1 d {};
    adapter1->GetDesc1(&d);
    if (d.AdapterLuid.HighPart == luid.HighPart && d.AdapterLuid.LowPart == luid.LowPart)
    {
      ComPtr<IDXGIAdapter3> adapter3;
      if (SUCCEEDED(adapter1.As(&adapter3)))
      {
        DXGI_QUERY_VIDEO_MEMORY_INFO info {};
        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        {
          m_vramBudgetGiB = double(info.Budget) / (1024.0 * 1024.0 * 1024.0);
          m_vramUsageGiB  = double(info.CurrentUsage) / (1024.0 * 1024.0 * 1024.0);
        }
      }
      break;
    }
    adapter1.Reset();
  }
}

void SceneGraphViewerApp::createTriangleCounterResources()
{
  ID3D12Device* dev = getDevice().Get();

  // === 1) UAV Counter: DEFAULT heap (4 bytes), ALLOW_UNORDERED_ACCESS ===
  {
    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC   descUav = CD3DX12_RESOURCE_DESC::Buffer(rbSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    dev->CreateCommittedResource(&heapDefault, D3D12_HEAP_FLAG_NONE, &descUav,

                                 D3D12_RESOURCE_STATE_COPY_DEST, // wir nullen per Copy aus Upload
                                 nullptr, IID_PPV_ARGS(&m_triCountUAV));

  }

  // === 2) Readback Buffer (4 bytes) ===
  {
    CD3DX12_HEAP_PROPERTIES heapReadback(D3D12_HEAP_TYPE_READBACK);
    CD3DX12_RESOURCE_DESC   descRb = CD3DX12_RESOURCE_DESC::Buffer(rbSize);

    dev->CreateCommittedResource(&heapReadback, D3D12_HEAP_FLAG_NONE, &descRb,

                                 D3D12_RESOURCE_STATE_COPY_DEST, // Ziel von CopyResource
                                 nullptr, IID_PPV_ARGS(&m_triCountReadback));

  }

  // === 3) Upload Zero (4 bytes, vorab mit 0 gefüllt) ===
  {
    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   descUp = CD3DX12_RESOURCE_DESC::Buffer(rbSize);

    dev->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &descUp, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                 IID_PPV_ARGS(&m_triZeroUpload));


    void* p = nullptr;
    // Für Upload-Heaps beim Schreiben: pReadRange = nullptr
    m_triZeroUpload->Map(0, nullptr, &p);
    std::memset(p, 0, 4);
    m_triZeroUpload->Unmap(0, nullptr);
  }
}

void SceneGraphViewerApp::collectMeshStats()
{
  // Vorframe lesen (der ist per Fence fertig)
  const UINT   frameIndexPrev = (getFrameIndex() + kFrameCount - 1) % kFrameCount;
  const UINT64 srcOffset      = UINT64(frameIndexPrev) * sizeof(UINT32);

  D3D12_RANGE r {srcOffset, srcOffset + sizeof(UINT32)};
  void*       p = nullptr;
  if (SUCCEEDED(m_triCountReadback->Map(0, &r, &p)))
  {
    const uint8_t* base = static_cast<uint8_t*>(p);
    uint32_t       tri  = 0;
    std::memcpy(&tri, base + srcOffset, sizeof(uint32_t));
    m_triCountFrame = tri;

    // kein CPU writeback
    D3D12_RANGE w {0, 0};
    m_triCountReadback->Unmap(0, &w);
  }

  // Historie / Avg / Max aktualisieren
  m_triHist[m_triHistIdx] = static_cast<float>(m_triCountFrame) / 1e6f; // Mio. Tri
  m_triHistIdx            = (m_triHistIdx + 1) % kTriHistory;

  double sum = 0;
  int    n   = 0;
  m_triMax   = 0.0;
  for (float v : m_triHist)
  {
    if (v > 0.0f)
    {
      sum += v;
      ++n;
      if (v > m_triMax)
        m_triMax = v;
    }
  }
  m_triAvg = (n > 0) ? (sum / n) : std::numeric_limits<double>::quiet_NaN();
}

