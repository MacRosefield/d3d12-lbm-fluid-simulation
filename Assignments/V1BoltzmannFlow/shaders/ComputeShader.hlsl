// ComputeShader.hlsl
Texture2D<float4> g_InputTexture : register(t0);
RWTexture2D<float4> g_OutputTexture : register(u0);

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


// for debugging
static const int g_KernelRadius = 5;
static const float g_Sigma = 2.0;
static const float2 g_TexelSize = float2(1.0 / 1920.0, 1.0 / 1080.0); // Annahme: Full HD

static const float PI = 3.14159265359;


// Berechnet das Gewicht für eine 1D-Gaussian-Kernel
float GaussianWeight(float x, float sigma)
{
    return exp(-0.5 * (x * x) / (sigma * sigma)) / (sqrt(2.0 * PI) * sigma);
}

[numthreads(16, 16, 1)]
void HorizontalBlur(uint3 threadID : SV_DispatchThreadID)
{
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);

    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;

    float4 color = 0;
    float weightSum = 0;
    
    for (int i = -g_KernelRadius; i <= g_KernelRadius; ++i)
    {
        int2 samplePos = int2(threadID.x + i, threadID.y);
        samplePos.x = clamp(samplePos.x, 0, texSize.x - 1);
        
        float weight = GaussianWeight(i, g_Sigma);
        color += g_InputTexture[samplePos] * weight;
        weightSum += weight;
    }
    g_OutputTexture[threadID.xy] = color / weightSum;
}

[numthreads(16, 16, 1)]
void VerticalBlur(uint3 threadID : SV_DispatchThreadID)
{
    int2 texSize;
    g_InputTexture.GetDimensions(texSize.x, texSize.y);

    if (threadID.x >= texSize.x || threadID.y >= texSize.y)
        return;

    float4 color = 0;
    float weightSum = 0;
    
    for (int i = -g_KernelRadius; i <= g_KernelRadius; ++i)
    {
        int2 samplePos = int2(threadID.x, threadID.y + i);
        samplePos.y = clamp(samplePos.y, 0, texSize.y - 1);
        
        float weight = GaussianWeight(i, g_Sigma);
        color += g_InputTexture[samplePos] * weight;
        weightSum += weight;
    }
    g_OutputTexture[threadID.xy] = color / weightSum;
}

