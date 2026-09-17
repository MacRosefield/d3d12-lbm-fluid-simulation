// DepthOfField.hlsl

Texture2D<float4> g_InputTexture : register(t0); // Originalbild
Texture2D<float4> g_BlurredTexture : register(t1); // Geblurrtes Bild
Texture2D<float> g_DepthTexture : register(t2); // Depth Texture (DSV)
RWTexture2D<float4> g_OutputTexture : register(u2);

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

cbuffer DoFParams : register(b0)
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
};

//static const float g_FocalDistance = 0.5; // Fokus-Distanz (Mittelwert der Tiefe)
static const float g_FocusRange = 0.1; // Bereich, in dem noch Schärfe vorhanden ist
static const float g_MaxBlur = 1.0; // Maximale Unschärfe (0 = kein Blur, 1 = voller Blur)

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);
    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;
 
    // Lese Tiefenwert aus Depth Texture (0 = nah, 1 = weit entfernt)
    float depth = g_DepthTexture[threadID.xy];

    // Berechne den Blur-Faktor basierend auf der Tiefe
    float blurAmount = saturate(abs(depth - cb_focalDistance) / g_FocusRange);
    blurAmount *= cb_blurStrength; // Maximaler Blur-Effekt

    // Lese die Farben aus den Texturen
    float4 sharpColor = g_InputTexture[threadID.xy];
    float4 blurredColor = g_BlurredTexture[threadID.xy];

    // Füge eine farbliche Tönung hinzu
    float4 sharpTint = sharpColor * float4(1.4, 0.8, 0.8, 1.0); // Rotstich für scharfe Bereiche
    float4 blurredTint = blurredColor * float4(0.8, 0.8, 1.4, 1.0); // Blaustich für unscharfe Bereiche

    // Lerp zwischen scharfem und unscharfem Bild mit Tönung
    g_OutputTexture[threadID.xy] = lerp(sharpTint, blurredTint, blurAmount);
}
