// FullScreenQuad.h
#ifndef FULLSCREEN_QUAD_CLASS
#define FULLSCREEN_QUAD_CLASS

#include "Drawable.h"
#include <d3d12.h>
#include <gimslib/types.hpp>
#include <stdexcept>
#include <vector>
#include <wrl.h>

/// <summary>
/// A simple fullscreen quad for post-processing effects or rendering fullscreen textures.
/// </summary>
class FullScreenQuad : public Drawable
{
public:
  /// <summary>
  /// Constructs a fullscreen quad.
  /// </summary>
  /// <param name="device">Device for GPU buffer creation.</param>
  /// <param name="commandQueue">Command queue for uploading data to the GPU.</param>
  FullScreenQuad(const Microsoft::WRL::ComPtr<ID3D12Device>&       device,
                 const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& commandQueue);

  /// <summary>
  /// Adds the commands necessary for rendering this fullscreen quad to the provided commandList.
  /// </summary>
  /// <param name="commandList">The command list.</param>
  void addToCommandList(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList) const override;

  /// <summary>
  /// Returns the input element descriptors required for the pipeline.
  /// </summary>
  /// <returns>The input element descriptor.</returns>
  static const std::vector<D3D12_INPUT_ELEMENT_DESC>& getInputElementDescriptors();

  FullScreenQuad();
  FullScreenQuad(const FullScreenQuad& other)                = default;
  FullScreenQuad(FullScreenQuad&& other) noexcept            = default;
  FullScreenQuad& operator=(const FullScreenQuad& other)     = default;
  FullScreenQuad& operator=(FullScreenQuad&& other) noexcept = default;

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;     //! The vertex buffer on the GPU.
  D3D12_VERTEX_BUFFER_VIEW               m_vertexBufferView; //! Vertex buffer view.

  //! Input element descriptor defining the vertex format.
  static const std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputElementDescs;

  //! Static vertex data for a fullscreen quad (in NDC).
  static const std::vector<float> m_vertexData;
};

#endif // FULLSCREEN_QUAD_CLASS
