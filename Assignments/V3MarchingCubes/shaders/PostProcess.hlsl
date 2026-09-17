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
  float  pad1;
  float3 color;
  float  intensity;
};

cbuffer CameraParameters : register(b0)
{
  matrix projectionMatrix;
  float3 cameraPosition;
  int    numOfLights;
  Light  lights[8];
  float3 boundingBoxColor;
  float  focalDistance;
  float  blurStrength;
  int    blurRadius;
  float  nearPlane;
  float  farPlane;
    float vigValue;
};

Texture2D<float>  g_textureDepth : register(t0); // Tiefentextur
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

// Funktion zum Rückrechnen der Tiefe in die Weltkoordinaten
float LinearizeDepth(float depth)
{
  return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

float4 PS_main(VertexShaderOutput input)
    : SV_Target
{

  // Tiefenwert an der aktuellen Texel-Koordinate abfragen
  float depth = g_textureDepth.Sample(g_sampler, input.texCoord);

  // Tiefenwert linearisieren
  float worldDepth = LinearizeDepth(depth);

  // Abstand zur fokussierten Entfernung berechnen
  float distanceToFocus = abs(worldDepth - focalDistance);

  // Stärke der Unschärfe basierend auf der Entfernung berechnen
  float blurAmount = saturate(distanceToFocus * blurStrength);

  // Box-Blur-Sampling um die Unschärfe zu erzeugen
  float2 texSize     = float2(1.0 / 640, 1.0 / 480);
  float4 color       = 0.0f;
  float  totalWeight = 0.0f;

  for (int x = -2; x <= 2; ++x)
  {
    for (int y = -2; y <= 2; ++y)
    {
      float2 offset = float2(x, y) * blurAmount * texSize;
      float4 sample = g_textureColor.Sample(g_sampler, input.texCoord + offset);

      // Gewichtung der Samples (kann auch durch einen Gauss-Filter ersetzt werden)
      float weight = 1.0 / (1.0 + distance(float2(x, y), 0.0));
      color += sample * weight;
      totalWeight += weight;
    }
  }

  // Finales Bild berechnen
  color /= totalWeight;

  return color;
}
