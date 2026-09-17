// FullScreenQuad.cpp
#include "FullScreenQuad.h"
#include <d3dx12/d3dx12.h>
#include <gimslib/d3d/UploadHelper.hpp>

// Vertex format: position (x, y)
const std::vector<float> FullScreenQuad::m_vertexData = {
    // Positions in NDC (x, y)
    -1.0f, -1.0f, // Bottom-left
     1.0f, -1.0f, // Bottom-right
    -1.0f,  1.0f, // Top-left
     1.0f,  1.0f  // Top-right
};

// Input element descriptors for the quad's vertex format
const std::vector<D3D12_INPUT_ELEMENT_DESC> FullScreenQuad::m_inputElementDescs = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
};

FullScreenQuad::FullScreenQuad(const Microsoft::WRL::ComPtr<ID3D12Device>& device,
                               const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& commandQueue)
{
    // Vertex buffer size
    const UINT vertexBufferSize = static_cast<UINT>(m_vertexData.size() * sizeof(float));

    // Create vertex buffer resource
    const CD3DX12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
    const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    if (FAILED(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_vertexBuffer))))
    {
        throw std::runtime_error("Failed to create vertex buffer resource.");
    }

    // Upload data to GPU using UploadHelper
    gims::UploadHelper uploadHelper(device, vertexBufferSize);
    uploadHelper.uploadBuffer(m_vertexData.data(), m_vertexBuffer, vertexBufferSize, commandQueue);

    // Setup vertex buffer view
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
    m_vertexBufferView.StrideInBytes = sizeof(float) * 2; // Two floats per vertex (x, y)
}

void FullScreenQuad::addToCommandList(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList) const
{
    if (!commandList)
    {
        throw std::invalid_argument("Command list is null.");
    }

    // Set vertex buffer and topology
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // Issue draw call for 2 triangles (4 vertices)
    commandList->DrawInstanced(4, 1, 0, 0);
}

const std::vector<D3D12_INPUT_ELEMENT_DESC>& FullScreenQuad::getInputElementDescriptors()
{
	return m_inputElementDescs;
}

FullScreenQuad::FullScreenQuad()
    : m_vertexBuffer()
    , m_vertexBufferView()
{
}