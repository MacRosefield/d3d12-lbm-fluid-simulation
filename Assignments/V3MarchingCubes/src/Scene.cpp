// Scene.cpp

#include "Scene.hpp"
#include "BoundingBox.h"
#include "ConstantBufferD3D12.hpp"
#include "PerMeshConstantBufferStruct.h"
#include <d3dx12/d3dx12.h>
#include <unordered_map>

void static addToCommandListImpl(Scene& scene, gims::ui32 nodeIdx, gims::f32m4 transformation,
                                 const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
                                 gims::ui32 modelViewRootParameterIdx, gims::ui32 materialConstantsRootParameterIdx,
                                 gims::ui32 srvRootParameterIdx, bool drawBoundingBox)
{

  if (nodeIdx >= scene.getNumberOfNodes())
  {
    return;
  }

  Node        currentNode               = scene.getNode(nodeIdx);
  gims::f32m4 accumulatedTransformation = transformation * currentNode.transformation;

  for (gims::ui32 i = 0; i < currentNode.meshIndices.size(); i++)
  {
    Material currMaterial = scene.getMaterial(scene.getMesh(currentNode.meshIndices[i]).getMaterialIndex());

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> materialSrvDescriptorHeap = currMaterial.srvDescriptorHeap;

    commandList->SetGraphicsRoot32BitConstants(modelViewRootParameterIdx, 16, &accumulatedTransformation, 0);

    commandList->SetGraphicsRootConstantBufferView(
        materialConstantsRootParameterIdx, currMaterial.materialConstantBuffer.getResource()->GetGPUVirtualAddress());

    commandList->SetDescriptorHeaps(1, materialSrvDescriptorHeap.GetAddressOf());

    commandList->SetGraphicsRootDescriptorTable(srvRootParameterIdx,
                                                materialSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    if (!drawBoundingBox)
    {
      scene.getMesh(currentNode.meshIndices[i]).addToCommandList(commandList);
    }
    else
    {
      scene.getBoundingBoxOfMesh(currentNode.meshIndices[i]).addToCommandList(commandList);
    }
  }

  for (const gims::ui32& nodeIndex : currentNode.childIndices)
  {
    addToCommandListImpl(scene, nodeIndex, accumulatedTransformation, commandList, modelViewRootParameterIdx,
                         materialConstantsRootParameterIdx, srvRootParameterIdx, drawBoundingBox);
  }
}

const Node& Scene::getNode(gims::ui32 nodeIdx) const
{
  return m_nodes[nodeIdx];
}

Node& Scene::getNode(gims::ui32 nodeIdx)
{
  return m_nodes[nodeIdx];
}

const gims::ui32 Scene::getNumberOfNodes() const
{
  return static_cast<gims::ui32>(m_nodes.size());
}

const gims::ui32 Scene::getNumberOfMeshes() const
{
  return static_cast<gims::ui32>(m_meshes.size());
}

const gims::ui32 Scene::getNumberOfMaterials() const
{
  return static_cast<gims::ui32>(m_materials.size());
}

const gims::ui32 Scene::getNumberOfTextures() const
{
  return static_cast<gims::ui32>(m_textures.size());
}

const TriangleMeshD3D12& Scene::getMesh(gims::ui32 meshIdx) const
{
  return m_meshes[meshIdx];
}

const BoundingBox& Scene::getBoundingBoxOfMesh(gims::ui32 meshIdx) const
{
  return m_BoundingBoxes[meshIdx];
}

const Material& Scene::getMaterial(gims::ui32 materialIdx) const
{
  return m_materials[materialIdx];
}

const AABB& Scene::getAABB() const
{
  return m_aabb;
}

void Scene::addToCommandList(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
                             const gims::f32m4 transformation, gims::ui32 modelViewRootParameterIdx,
                             gims::ui32 materialConstantsRootParameterIdx, gims::ui32 srvRootParameterIdx)
{
  addToCommandListImpl(*this, 0, transformation, commandList, modelViewRootParameterIdx,
                       materialConstantsRootParameterIdx, srvRootParameterIdx, false);
}

void Scene::addToCommandListBoundingBox(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
                                        const gims::f32m4 transformation, gims::ui32 modelViewRootParameterIdx,
                                        gims::ui32 materialConstantsRootParameterIdx, gims::ui32 srvRootParameterIdx)
{
  addToCommandListImpl(*this, 0, transformation, commandList, modelViewRootParameterIdx,
                       materialConstantsRootParameterIdx, srvRootParameterIdx, true);
}

void Scene::addToCommandListDepth(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
                                  const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>&      depthSrvDescriptorHeap,
                                  const gims::ui32                                         srvRootParameterIdx)
{
  if (!commandList)
  {
    throw std::invalid_argument("Command list is null.");
  }

  if (!depthSrvDescriptorHeap)
  {
    throw std::invalid_argument("Depth SRV Descriptor Heap is null.");
  }

  // Set descriptor heap for the depth texture (t0 in the shader)
  commandList->SetDescriptorHeaps(1, depthSrvDescriptorHeap.GetAddressOf());

  // Bind the depth texture descriptor to the root signature
  commandList->SetGraphicsRootDescriptorTable(srvRootParameterIdx,
                                              depthSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

  // Render the full-screen quad
  m_quad.addToCommandList(commandList);
}

void Scene::addToCommandListPost(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
                                 const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>&      postSrvDescriptorHeap,
                                 const gims::ui32                                         srvRootParameterIdx)
{
  if (!commandList)
  {
    throw std::invalid_argument("Command list is null.");
  }

  if (!postSrvDescriptorHeap)
  {
    throw std::invalid_argument("Depth SRV Descriptor Heap is null.");
  }

  // Set descriptor heap for the depth texture (t0 in the shader)
  commandList->SetDescriptorHeaps(1, postSrvDescriptorHeap.GetAddressOf());

  // Bind the depth texture descriptor to the root signature
  commandList->SetGraphicsRootDescriptorTable(srvRootParameterIdx,
                                              postSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

  // Render the full-screen quad
  m_quad.addToCommandList(commandList);
}