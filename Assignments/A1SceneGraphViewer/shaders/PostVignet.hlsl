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

//static const float vignetteIntensity = 1.0f;

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    // Hole die Dimensionen des Bildes
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);
    
    // Abbruch, falls außerhalb der Grenzen
    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;

    // Berechne die UV-Koordinaten im Bereich [0, 1]
    float2 uv = float2(threadID.x, threadID.y) / float2(texSize);

    // Verschiebe und skaliere die UVs, sodass der Bildmittelpunkt bei (0,0) liegt und die Werte von -1 bis 1 gehen
    float2 centeredUV = (uv - 0.5) * 2.0;

    // Berechne den Abstand vom Zentrum
    float dist = length(centeredUV);

    // Erzeuge einen Vignetten-Faktor:
    // Mit smoothstep wird ein weicher Übergang definiert: 
    // Bei dist <= 0.5 wird der Faktor nahe 1.0, bei dist >= 1.0 nahe 0.0.
    float rawVignette = smoothstep(1.0, 0.5, dist);
    // Interpolieren zwischen keiner Abdunklung (1.0) und dem berechneten Wert anhand der Intensität.
    float vignette = lerp(1.0, rawVignette, vignetIntes);

    // Hole die Originalfarbe
    float4 color = g_InputTexture[threadID.xy];

    // Wende den Vignetten-Effekt an
    g_OutputTexture[threadID.xy] = color * vignette;
}
