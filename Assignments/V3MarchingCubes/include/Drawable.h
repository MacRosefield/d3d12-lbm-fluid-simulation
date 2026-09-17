// Drawable.h
#ifndef DRAWABLE_CLASS
#define DRAWABLE_CLASS

#include <d3d12.h>
#include <vector>
#include <wrl.h>

/// <summary>
/// Abstract base class for drawable objects.
/// </summary>
class Drawable
{
public:
    virtual ~Drawable() = default;

    /// <summary>
    /// Adds the commands necessary for rendering this object to the provided commandList.
    /// </summary>
    /// <param name="commandList">The command list.</param>
    virtual void addToCommandList(const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList) const = 0;
};
#endif // DRAWABLE_CLASS
