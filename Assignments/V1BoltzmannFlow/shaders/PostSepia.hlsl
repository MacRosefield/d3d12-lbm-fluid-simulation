// VignetteEffect.hlsl

// Eingabe-Bild als SRV (t0)
Texture2D<float4> g_InputTexture : register(t0);
// Ausgabe-Bild als UAV (u0)
RWTexture2D<float4> g_OutputTexture : register(u2);

// Parameter für den Vignetten-Effekt
struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

cbuffer VignetParams : register(b0)
{
    matrix projectionMatrix;
    float3 cameraPosition;
    int numOfLights;
    Light lights[8];
    float3 boundingBoxColor;
    float cb_focalDistance;
    float cb_blurStrength;
    int blurRadius;
    float nearPlane;
    float farPlane;
    float vignetIntes;
};



[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    // Hole die Dimensionen des Bildes
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y); // Abbruch, falls außerhalb der Grenzen
    
    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;

    // Load the source pixel color (assumes sRGB if needed, adjust if linear space)
    float4 srcColor = g_InputTexture.Load(int3(threadID.xy, 0));

    // Apply the sepia effect.
    // The sepia tone is computed with weighted dot products:
    float sepiaR = dot(srcColor.rgb, float3(0.393, 0.769, 0.189));
    float sepiaG = dot(srcColor.rgb, float3(0.349, 0.686, 0.168));
    float sepiaB = dot(srcColor.rgb, float3(0.272, 0.534, 0.131));

    // Clamp the results to [0, 1] to avoid overflow (if your texture is normalized)
    float3 sepiaColor = saturate(float3(sepiaR, sepiaG, sepiaB));

    // Write the sepia color to the output texture, preserving alpha.
    g_OutputTexture[threadID.xy] = float4(sepiaColor, srcColor.a);
}