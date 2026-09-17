// SceneGraphViewerApp.cpp

#include "SceneGraphViewerApp.hpp"
#include "ConstantBufferStruct.h"
#include "LBMCelltypes.h"
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

  // #MH compute Pipeline initialization
  createGraphicPipeline();
  createComputePipeline();
  createLBMPipeline();

  createSceneConstantBuffer();

  createSRVandUAV();
  CreateLBMBuffers();
  InitializeLBMGridWithStreifen();
  // InitializeCelltypeCondition();

  updateUiDataStruct();
}

void SceneGraphViewerApp::onResize()
{
  createSRVandUAV();
}

void SceneGraphViewerApp::onDraw()
{
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

  // default render target
  commandList->OMSetRenderTargets(1, &m_offscreenTarget_CPU_RTV, FALSE, &dsvHandle);

  const float clearColor[] = {m_uiData.backgroundColor.x, m_uiData.backgroundColor.y, m_uiData.backgroundColor.z, 1.0f};

  commandList->ClearRenderTargetView(m_offscreenTarget_CPU_RTV, clearColor, 0, nullptr);
  commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  commandList->RSSetViewports(1, &getViewport());
  commandList->RSSetScissorRects(1, &getRectScissor());

  drawScene(commandList);

  // Transition RTV offscreen-target to UAV
  {
    CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    commandList->ResourceBarrier(_countof(barriers), barriers);
  }

  // #MH COMPUTE FOG EFFECT
  if (m_uiData.displayPostFog)
  {

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateFog.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    ////  FOG effect Original Image -> Fog Image
    commandList->SetComputeRootDescriptorTable(0, m_offscreenTarget_GPU_SRV); // Originalbild
    commandList->SetComputeRootDescriptorTable(4, m_depthTexture_GPU_SRV);    // Depth Map (DSV)
    commandList->SetComputeRootDescriptorTable(5, m_finalTarget_GPU_UAV);     // Finales Ausgabe-Bild

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(9, cb); // 9 add Constant Buffer for lighting

    const ui32v3 threadGroupSize(16, 16, 1);
    commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
                          ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);

    if (!m_uiData.displayPostDOF)
    {

      {
        CD3DX12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                 D3D12_RESOURCE_STATE_COPY_DEST)};

        commandList->ResourceBarrier(_countof(barriers), barriers);
      }
      commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
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

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(m_offscreenTarget.Get(), m_finalTarget.Get());
    // 8. Transition all targets back to render target

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      };
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  // #MH COMPUTE DOF IMAGE
  if (m_uiData.displayPostDOF)
  {

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateHorizontal.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    //// Horizontaler Blur: Offscreen -> PingPong**
    // commandList->SetComputeRootDescriptorTable(0, m_offscreenTarget_GPU_SRV); // Setzt SRV input
    // commandList->SetComputeRootDescriptorTable(1, m_gaussianBlur_X_GPU_UAV);  // Setzt UAV output
    // const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    // commandList->SetComputeRootConstantBufferView(9, cb); // 9 add Constant Buffer for lighting
    // const ui32v3 threadGroupSize(16, 16, 1);
    // commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
    //                       ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);
    //  GPU-Synchronisation**
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_gaussianTarget_x.Get());
    commandList->ResourceBarrier(1, &uavBarrier);

    // Vertikaler Blur : PingPong -> Offscreen* *
    /*commandList->SetPipelineState(m_computePipelineStateVertical.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetComputeRootDescriptorTable(0, m_gaussianBlur_X_GPU_SRV);
    commandList->SetComputeRootDescriptorTable(1, m_gaussianBlur_Y_GPU_UAV);
    commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
                          ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);*/

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

    ID3D12Resource* readBuffer;
    ID3D12Resource* writeBuffer;

    if (mUseBufferA)
    {
      readBuffer  = m_GridBufferA.Get();
      writeBuffer = m_GridBufferB.Get();
    }
    else
    {
      readBuffer  = m_GridBufferB.Get();
      writeBuffer = m_GridBufferA.Get();
    }

    commandList->SetComputeRootUnorderedAccessView(0, readBuffer->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(1, writeBuffer->GetGPUVirtualAddress());
    commandList->SetComputeRootDescriptorTable(2, m_finalTarget_GPU_UAV);
    commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer->GetGPUVirtualAddress()); // u3

    commandList->Dispatch(640 / 16, 480 / 16, 1);
    // mUseBufferA = !mUseBufferA;
    //    ##### LBM Streaming-Schritt
    commandList->SetPipelineState(m_computePipelineStateLBMStream.Get());
    commandList->SetComputeRootUnorderedAccessView(
        0, writeBuffer->GetGPUVirtualAddress()); // read = was Collision geschrieben hat
    commandList->SetComputeRootUnorderedAccessView(1, readBuffer->GetGPUVirtualAddress()); // write = zurück in alten
    commandList->SetComputeRootUnorderedAccessView(3, m_CellTypeBuffer->GetGPUVirtualAddress()); // u3

    commandList->Dispatch(640 / 16, 480 / 16, 1);

    // mUseBufferA = !mUseBufferA;

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

  // #MH RENDERING CLEAR IMAGE
  if (!m_uiData.displayPostDOF && !m_uiData.displayPostFog)
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

#if 1
  if (m_uiData.displayPostVignet)
  {

    // GET RENDER IMAGE
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(m_offscreenTarget.Get(), getRenderTarget().Get());

    // 8. Transition all targets back to render target
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateVignet.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    ////  FOG effect Original Image -> Fog Image
    commandList->SetComputeRootDescriptorTable(0, m_offscreenTarget_GPU_SRV); // Originalbild
    commandList->SetComputeRootDescriptorTable(5, m_finalTarget_GPU_UAV);     // Finales Ausgabe-Bild

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(9, cb); // 9 add Constant Buffer for lighting

    const ui32v3 threadGroupSize(16, 16, 1);
    commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
                          ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
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
    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  if (m_uiData.displayPostSepia)
  {

    // GET RENDER IMAGE
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(m_offscreenTarget.Get(), getRenderTarget().Get());

    // 8. Transition all targets back to render target
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateSepia.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    ////  FOG effect Original Image -> Fog Image
    commandList->SetComputeRootDescriptorTable(0, m_offscreenTarget_GPU_SRV); // Originalbild
    commandList->SetComputeRootDescriptorTable(5, m_finalTarget_GPU_UAV);     // Finales Ausgabe-Bild

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(9, cb); // 9 add Constant Buffer for lighting

    const ui32v3 threadGroupSize(16, 16, 1);
    commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
                          ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
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
    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

  if (m_uiData.displayPostGrain)
  {

    // GET RENDER IMAGE
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(m_offscreenTarget.Get(), getRenderTarget().Get());

    // 8. Transition all targets back to render target
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET),
          CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }

    // Bind offscreen-target as texture to compute shader
    commandList->SetPipelineState(m_computePipelineStateGrain.Get());
    commandList->SetComputeRootSignature(m_computeRootSignature.Get());
    commandList->SetDescriptorHeaps(1, m_descriptorHeap.GetAddressOf());

    ////  FOG effect Original Image -> Fog Image
    commandList->SetComputeRootDescriptorTable(0, m_offscreenTarget_GPU_SRV); // Originalbild
    commandList->SetComputeRootDescriptorTable(5, m_finalTarget_GPU_UAV);     // Finales Ausgabe-Bild

    const D3D12_GPU_VIRTUAL_ADDRESS cb = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(9, cb); // 9 add Constant Buffer for lighting

    const ui32v3 threadGroupSize(16, 16, 1);
    commandList->Dispatch(ui32(ceilf(getWidth() / f32(threadGroupSize.x))),
                          ui32(ceilf(getHeight() / f32(threadGroupSize.y))), 1);

    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {
          CD3DX12_RESOURCE_BARRIER::Transition(m_finalTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                               D3D12_RESOURCE_STATE_COPY_SOURCE),
          CD3DX12_RESOURCE_BARRIER::Transition(getRenderTarget().Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                               D3D12_RESOURCE_STATE_COPY_DEST)};

      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
    commandList->CopyResource(getRenderTarget().Get(), m_finalTarget.Get());
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
    // Transition RTV offscreen-target to UAV
    {
      CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::Transition(
          m_offscreenTarget.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET)};
      commandList->ResourceBarrier(_countof(barriers), barriers);
    }
  }

#endif

  // set window as render target
  commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
}

void SceneGraphViewerApp::onDrawUI()
{
  const ImGuiWindowFlags_ imGuiFlags =
      m_examinerController.active() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;

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
  ImGui::Checkbox("Display Depth Texture", &m_uiData.displayDepthTexture); // Depth Texture

  ImGui::Text("Depth fog settings:");
  ImGui::Checkbox("Display Fog", &m_uiData.displayPostFog); // Fog toggle

  // Vignet
  ImGui::Text("Vigent settings:");
  ImGui::Checkbox("Display Vignet", &m_uiData.displayPostVignet); // Vigent effect
  ImGui::SliderFloat("Vignet intensity", &m_uiData.vignetIntes, 0.1f, 1.0f);

  // Sepia setup
  ImGui::Text("Sepia settings:");
  ImGui::Checkbox("Display Sepia", &m_uiData.displayPostSepia); // Vigent effect

  // grain
  ImGui::Text("Film grain settings:");
  ImGui::Checkbox("Display Grain", &m_uiData.displayPostGrain); // Grain effect
  ImGui::SliderFloat("Grain intensity", &m_uiData.grainIntes, 0.1f, 1.0f);

  // Depth-of-Field setup
  ImGui::Text("Depth of field settings:");
  ImGui::Checkbox("Display Depth of Field", &m_uiData.displayPostDOF); // Depth of field
  ImGui::SliderFloat("Focal Distance", &m_uiData.focalDistance, 0.1f, 1.0f);
  ImGui::SliderFloat("Blur Strength", &m_uiData.blurStrength, 0.0f, 1.0f);
  ImGui::SliderInt("Blur Radius", &m_uiData.blurRadius, 3, 5);

  ImGui::Text("Camera settings:");
  ImGui::SliderFloat("Near Plane", &m_uiData.nearPlane, 0.0f, 16.0f);
  ImGui::SliderFloat("Far Plane", &m_uiData.farPlane, 0.0f, 16.0f);

  if (ImGui::Button("Defaults")) // Reseting changed UiData
  {
    m_uiData = UiData();
    updateUiDataStruct();
  }
  ImGui::End(); // Emd of Configuration Window

  ImGui::Begin("Kamera Einstellungen");

  ImGui::Text("Get Cam values:");

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

  ImGui::Text("Set Cam Presets:");

  if (ImGui::Button("Preset 1"))
  {
    // Statische Variable, damit die Eingabewerte zwischen Frames erhalten bleiben.
    static float translation[3] = {0.0f, -0.25f, 1.5f};
    ImGui::InputFloat3("Neue Translation", translation);
    // Umwandlung in deinen f32v3-Typ, falls nötig.
    f32v3 newTranslation = {translation[0], translation[1], translation[2]};
    m_examinerController.setTranslationVector(newTranslation);

    static float rotation[4] = {-1.0f, 0.0f, 0.0f, -0.0f};
    ImGui::InputFloat4("Neue Rotation (Quaternion)", rotation);
    // Umwandlung in deinen f32q-Typ, falls nötig.
    f32q newRotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
    m_examinerController.setRotationQuaterion(newRotation);
  }
  if (ImGui::Button("Preset 2"))
  {
    // Statische Variable, damit die Eingabewerte zwischen Frames erhalten bleiben.
    static float translation[3] = {0.00695192f, 0.101838f, 0.395462f};
    ImGui::InputFloat3("Neue Translation", translation);
    // Umwandlung in deinen f32v3-Typ, falls nötig.
    f32v3 newTranslation = {translation[0], translation[1], translation[2]};
    m_examinerController.setTranslationVector(newTranslation);

    static float rotation[4] = {0.70049f, 0.0403086f, -0.711239f, -0.0427657f};
    ImGui::InputFloat4("Neue Rotation (Quaternion)", rotation);
    // Umwandlung in deinen f32q-Typ, falls nötig.
    f32q newRotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
    m_examinerController.setRotationQuaterion(newRotation);
  }

  ImGui::End();
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

void SceneGraphViewerApp::createGraphicPipeline()
{
  waitForGPU();
  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs = TriangleMeshD3D12::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescsBB = BoundingBox::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescsQuad = FullScreenQuad::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementPT = FullScreenQuad::getInputElementDescriptors();

  const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementPostProcessing = FullScreenQuad::getInputElementDescriptors();

  const ComPtr<IDxcBlob> vertexShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/TriangleMesh.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/TriangleMesh.hlsl", L"PS_main", L"ps_6_0");
  const ComPtr<IDxcBlob> vertexShaderBB =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/BoundingBox.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderBB =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/BoundingBox.hlsl", L"PS_main", L"ps_6_0");
  const ComPtr<IDxcBlob> vertexShaderDepth =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/DepthBuffer.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderDepth =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/DepthBuffer.hlsl", L"PS_main", L"ps_6_0");
  // const ComPtr<IDxcBlob> vertexShaderQuad =
  //     compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/FullScreenQuad.hlsl", L"VS_main", L"vs_6_0");
  // const ComPtr<IDxcBlob> pixelShaderQuad =
  //     compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/FullScreenQuad.hlsl", L"PS_main", L"ps_6_0");

  // #MH PassThrough shader configuration
  const ComPtr<IDxcBlob> vertexShaderPT =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PassThrough.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderPT =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PassThrough.hlsl", L"PS_main", L"ps_6_0");

  // #MH Post processing shader configuration
  const ComPtr<IDxcBlob> vertexShaderPP =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostProcess.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderPP =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostProcess.hlsl", L"PS_main", L"ps_6_0");

  // #MH Post processing shader configuration
  const ComPtr<IDxcBlob> vertexShaderLut =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostLUT.hlsl", L"VS_main", L"vs_6_0");
  const ComPtr<IDxcBlob> pixelShaderLut =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostLUT.hlsl", L"PS_main", L"ps_6_0");

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

  m_scene.addToCommandList(cmdLst, transform, 1, 2, 3);

  if (m_uiData.displayBoundingBoxes)
  {
    cmdLst->SetPipelineState(m_pipelineStateBB.Get());

    m_scene.addToCommandListBoundingBox(cmdLst, transform, 1, 2, 3);
  }

  // if (m_uiData.displayDepthTexture)
  //{
  //  cmdLst->SetPipelineState(m_pipelineStateDepthTexture.Get());
  //  cmdLst->SetGraphicsRootSignature(m_rootSignatureDepth.Get());
  //  createDepthStencilSRV(getDepthStencil());
  //  m_scene.addToCommandListDepth(cmdLst, m_depthSrvDescriptorHeap, 0);
  //}

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

  static auto startTime   = std::chrono::high_resolution_clock::now();
  auto        currentTime = std::chrono::high_resolution_clock::now();
  float       timeElapsed = std::chrono::duration<float>(currentTime - startTime).count();

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

  cb.time = timeElapsed;

  cb.gridWidth  = 640;
  cb.gridHeight = 480;
  // cb.tau        = 0.6;

  m_constantBuffers[getFrameIndex()].upload(&cb);
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
  const auto horizontalComputeShader = compileShader(
      L"../../../Assignments/A1SceneGraphViewer/Shaders/ComputeShader.hlsl", L"HorizontalBlur", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                    = m_computeRootSignature.Get();
  psoDesc.CS                                = HLSLCompiler::convert(horizontalComputeShader);
  psoDesc.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateHorizontal)));

  // 2 **Vertikaler Blur Compute Shader laden**
  const auto verticalComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/ComputeShader.hlsl", L"VerticalBlur", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(verticalComputeShader); // **Nur Shader ändern, RootSignature bleibt gleich!**

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateVertical)));

  // **3 Depth of Field Pipeline**
  const auto dofComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/DepthOfField.hlsl", L"main", L"cs_6_0");

  psoDesc.CS = HLSLCompiler::convert(dofComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateDoF)));

  // 4 Fog Pipeline
  const auto fogComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/FogEffect.hlsl", L"main", L"cs_6_0");
  psoDesc.CS = HLSLCompiler::convert(fogComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateFog)));

  // 5 Vignet Pipeline
  const auto vignetComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostVignet.hlsl", L"main", L"cs_6_0");
  psoDesc.CS = HLSLCompiler::convert(vignetComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateVignet)));

  // 6 Grain Pipeline
  const auto grainComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostGrain.hlsl", L"main", L"cs_6_0");
  psoDesc.CS = HLSLCompiler::convert(grainComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateGrain)));

  // 7 Sepia Pipeline
  const auto sepiaComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/PostSepia.hlsl", L"main", L"cs_6_0");
  psoDesc.CS = HLSLCompiler::convert(sepiaComputeShader);
  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateSepia)));
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
  heapDesc.NumDescriptors = 8;
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
}

void SceneGraphViewerApp::createLBMComputeRootSignature()
{

  // Wir brauchen 3 Parameter:
  //  - Parameter[0]: RWStructuredBuffer an u0 (Root UAV)
  //  - Parameter[1]: RWStructuredBuffer an u1 (Root UAV)
  //  - Parameter[2]: Descriptor Table für typisierte UAV(s) an u2

  CD3DX12_ROOT_PARAMETER parameters[4] = {};

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

void SceneGraphViewerApp::createLBMPipeline()
{
  const auto lbmComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/LBM.hlsl", L"main", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature                    = m_LBMcomputeRootSignature.Get();
  psoDesc.CS                                = HLSLCompiler::convert(lbmComputeShader);
  psoDesc.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;

  throwIfFailed(getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_computePipelineStateLBM)));

  // 2 ** Streaming step **
  const auto lbmStreamComputeShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/LBMStreaming.hlsl", L"main", L"cs_6_0");

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

  const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSizeInBytes);

  const auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(m_GridBufferA.GetAddressOf()));

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(m_GridBufferB.GetAddressOf()));

  // ===== BUFFER FUER ZELLTYPES =====
  const auto cellBufferSizeInBytes = static_cast<UINT64>(cellCount) * sizeof(ui32);

  const auto cellBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cellBufferSizeInBytes);

  getDevice()->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &cellBufferDesc,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                       IID_PPV_ARGS(m_CellTypeBuffer.GetAddressOf()));
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

  for (int y = 0; y < gridHeight; ++y)
  {
    for (int x = 0; x < gridWidth; ++x)
    {
      int   idx  = y * gridWidth + x;
      auto& cell = initialGrid[idx];

      // === Zelltyp bestimmen ===
      //    = WALL 4 =
      if (y == 0 || y == gridHeight - 1)
      {
        cellTypes[idx] = 4; // Cell_Wall
      }

      //    = OUTFLOW 2 =
      else if (x == gridWidth - 1)
      {
        cellTypes[idx] = 2; // Cell_OUTFLOW
      }

           //    = INFLOW 1 =
      else if (x == 0 )
      {
        cellTypes[idx] = 1; // Cell_Inflow
        std::cout << "Set inflow at: " << idx << std::endl;
      }

      else
      {
        cellTypes[idx] = 0; // FLUID
      }

      cell.rho = 1.0f;

      if (cellTypes[idx] == 4 /* Wall */)
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
        cell.u = {0.0f, 0.0f};
        for (int i = 0; i < 9; ++i)
          // cell.f[i] = 0.0f; // oder: feq mit u = 0
          cell.f[i] = w[i];
      }
      else if (cellTypes[idx] == 1 /* INFLOW */)
      {
        // Keine Geschwindigkeit, keine Bewegung
        cell.u = {0.1f, 0.0f};
        cell.rho = 1.0f;
        for (int i = 0; i < 9; ++i)
        {
          float cu  = c[i].x * cell.u.x + c[i].y * cell.u.y;
          cell.f[i] = w[i] * cell.rho *
                      (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * (cell.u.x * cell.u.x + cell.u.y * cell.u.y));
        }
      }



      else
      {
        // Horizontaler Streifen in der Mitte
        if (y >= gridHeight / 2 - 80 && y <= gridHeight / 2 + 80)
        {
          cell.u = {0.05f, 0.05f * sinf(x * 0.1f)};
        }
        else
        {
          cell.u = {0.0f, 0.0f};
        }

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

/*

void SceneGraphViewerApp::InitializeCelltypeCondition()
{
  const int  gridWidth  = 640;
  const int  gridHeight = 480;
  const UINT cellCount  = gridWidth * gridHeight;

  // === CellType-Daten vorbereiten ===
  std::vector<uint32_t> cellTypes(cellCount, 0); // 0 = Cell_Fluid

  for (int y = 0; y < gridHeight; ++y)
  {
    for (int x = 0; x < gridWidth; ++x)
    {
      const uint32_t idx = y * gridWidth + x;

      // Obere und untere Wand = Cell_Wall (Typ 4)
      if (y == 0 || y == gridHeight - 1)
      {
        cellTypes[idx] = 4; // Cell_Wall
      }
    }
  }

  // === Upload via UploadHelper ===
  const size_t bufferSize = cellCount * sizeof(uint32_t);
  UploadHelper uploadHelper(getDevice(), bufferSize);

  uploadHelper.uploadDefaultBuffer(cellTypes.data(), m_CellTypeBuffer, bufferSize, getCommandQueue());
}

*/