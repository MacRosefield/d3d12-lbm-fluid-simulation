// FullScreenQuad.hlsl

struct VertexInput
{
  float3 position : POSITION;
};

struct VertexShaderOutput
{
  float4 clipSpacePosition : SV_POSITION;
  float2 texCoord : TEXCOOD;
};

Texture2D<float4> g_textureDepth : register(t0);

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

  // Sample the rendered texture using the interpolated texture coordinates
  float4 color = g_textureDepth.Sample(g_sampler, input.texCoord);

  // Output the final color with original alpha
    return float4(color.x, color.y, color.z, 1.0f);
}