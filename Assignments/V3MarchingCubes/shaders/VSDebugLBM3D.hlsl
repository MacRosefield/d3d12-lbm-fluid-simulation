// VSDebugLBM3D.hlsl

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};


struct LBCell3D
{
    float rho;
    float3 u;
    float f[19];
};

cbuffer PerFrameConstants : register(b0)
{
    float4x4 projectionMatrix;
    float3 cameraPosition;
    int numOfLights;
    Light lights[8];
    float3 boundingBoxColor;
    float focalDistance;
    float blurStrength;
    int blurRadius;
    float nearPlane;
    
    float farPlane;
    
    float vigValue;
    float grainValue;
    
    float colorIntens;
    float inflow_u;
    float outflow_u;
    
    int debugPlanePos;
};

cbuffer PerMeshConstants : register(b1)
{
    float4x4 modelViewMatrix;
};


struct VSInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct PSInput
{
    
    float4 clipSpacePosition : SV_POSITION;
    float2 texcoord : TEXCOORD;
};


PSInput VS_main(VSInput input)
{
    PSInput output;

    
    float4 p4 = mul(modelViewMatrix, float4(input.position.xy, (float) debugPlanePos, 1.0f));

    output.clipSpacePosition = mul(projectionMatrix, p4);
    output.texcoord = input.texcoord;
    return output;
}

