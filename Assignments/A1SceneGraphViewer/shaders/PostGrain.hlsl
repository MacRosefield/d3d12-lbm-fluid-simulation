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

cbuffer GrainParams : register(b0)
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
    float grainIntensity;
    float time;
};

static const float vignetteIntensity = 1.0f;
//static const float time = 2;
//static const float grainIntensity = 0.4f;
/*
cbuffer Constants : register(b0)
{
    float grainIntensity; // Stärke des Film Grain
    float time; // Zeitvariable für animiertes Rauschen
};
*/
uint Hash(uint x)
{
    x = (x ^ 61) ^ (x >> 16);
    x *= 9;
    x ^= x >> 4;
    x *= 0x27d4eb2d;
    x ^= x >> 15;
    return x;
}

float Random(uint2 seed)
{
    uint hash = Hash(seed.x + 374761393 + seed.y * 668265263);
    return frac(float(hash) / float(0xFFFFFFFF));
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);
    if (DTid.x >= texSize.x || DTid.y >= texSize.y)
        return;

    float4 color = g_InputTexture[DTid.xy];

    // Erzeuge Film Grain Rauschen mit einer Pseudozufallsfunktion
    float noise = Random(DTid.xy + uint2(time * 1000.0f, time * 2000.0f)) * 2.0 - 1.0;
    noise *= grainIntensity;

    // Füge das Rauschen zur Luminanz hinzu
    float lum = dot(color.rgb, float3(0.299, 0.587, 0.114));
    lum += noise;
    
    // Begrenze Werte auf gültigen Bereich
    lum = saturate(lum);

    // Mische die originale Farbe mit dem neuen Luminanzwert
    float3 grainyColor = lerp(color.rgb, lum.xxx, grainIntensity);

    g_OutputTexture[DTid.xy] = float4(grainyColor, color.a);
}
