// FogEffect.hlsl

// Originalbild als SRV (t0)
Texture2D<float4> g_InputTexture : register(t0);
Texture2D<float4> g_BlurredTexture : register(t1); // Geblurrtes Bild
// Depth-Map als SRV (t1) – erwartet einen linearen Tiefenwert oder einen Wert, den man linearisieren kann
Texture2D<float> g_DepthTexture : register(t2);
// Ausgabe-Ziel als UAV (u0)
RWTexture2D<float4> g_OutputTexture : register(u2);

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

cbuffer FogValues : register(b0)
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



    static const float fogDensity = 1.0; // Steuerung, wie stark der Nebel wirkt (z.B. 1.0)
    static const float3 fogColor = { 0.8, 0.8, 0.8 }; // Nebelfarbe, z.B. (0.8, 0.8, 0.8) für grauen Nebel

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    // Hole die Dimensionen des Eingabebildes
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);

    // Abbruch, falls wir außerhalb der Bildgrenzen liegen
    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;

    // Lese Originalfarbe und Depth-Wert
    float4 originalColor = g_InputTexture[threadID.xy];
    float depth = g_DepthTexture[threadID.xy];

    
    // Normiere den Tiefenwert (angenommen, depth ist linear oder bereits linearisiert)
    float linearDepth = saturate((depth - nearPlane) / (farPlane - nearPlane));

    // Berechne den Nebelfaktor. Hier wird ein linearer Nebel genutzt, der durch den Faktor 'fogDensity' gesteuert wird.
    // Alternativ könnte man auch einen exponentiellen oder anderen Verlauf wählen.
    float fogFactor = saturate(linearDepth * fogDensity);

    // Mische Originalfarbe und Nebelfarbe entsprechend dem Faktor
    float4 finalColor = lerp(originalColor, float4(fogColor, originalColor.a), fogFactor);

    // Schreibe das Ergebnis in das Ausgabe-Ziel
    g_OutputTexture[threadID.xy] = finalColor;
}