// DepthBuffer.hlsl

struct VertexInput
{
    float3 position : POSITION;
};

struct VertexShaderOutput
{
    float4 clipSpacePosition : SV_POSITION;
    float2 texCoord : TEXCOOD;
};

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

cbuffer CameraParameters : register(b0)
{
    matrix projectionMatrix;
    float3 cameraPosition;
    int numOfLights;
    Light lights[8];
    float3 boundingBoxColor;
    float focalDistance;
    float blurStrength;
    int blurRadius;
    float nearPlane;
    float farPlane;
};

Texture2D<float> g_textureDepth : register(t0); // Tiefentextur
Texture2D<float4> g_textureColor : register(t1); // Farbetextur

SamplerState g_sampler : register(s0);

VertexShaderOutput VS_main(VertexInput input)
{
    VertexShaderOutput output;

  // Pass position directly to the clip space
    output.clipSpacePosition = float4(input.position.x, -input.position.y, input.position.z, 1.0f);

  // Derive texture coordinates from clip-space position
    output.texCoord = input.position.xy * 0.5 + 0.5; // Map [-1, 1] to [0, 1]

    return output;
}



float4 PS_main(VertexShaderOutput input)
    : SV_Target
{

  // Textur durchreichen
    float4 color = g_textureColor.Sample(g_sampler, input.texCoord);

    return color;
}
