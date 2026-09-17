// PSDebugLBM3D.hlsl

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
    float m;
    float epsilon;
};


cbuffer PerFrameConstants : register(b0)
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
    
    float vigValue;
    float grainValue;
    
    float colorIntens;
    float inflow_u;
    float outflow_u;
    
    int debugPlanePos;
    
    float b_tau;
    
    int gridWidth;
    int gridHeight;
    int gridDepth;
    
    
    
};

cbuffer PerMeshConstants : register(b1)
{
    float4x4 cameraMatrix;
};


struct PSInput
{
    float4 viewSpacePosition : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

StructuredBuffer<uint32_t> cellTypes : register(t2);
RWStructuredBuffer<LBCell3D> lbmCell : register(u0);

float4 PS_celltype(PSInput input) : SV_TARGET
{
    
    //uint gridWidth = 128;
    //uint gridHeight = 96;
    //uint gridDepth = 64;
    
    
    // Schritt 1: Texcoords Pixelkoordinaten
    uint x = uint(input.texcoord.x * gridWidth);
    uint y = uint(input.texcoord.y * gridHeight);
    uint z = debugPlanePos;
    // Schritt 2: Index berechnen
    uint index = z * gridWidth * gridHeight + y * gridWidth + x;

    // Schritt 3: Wert lesen
    uint32_t value = cellTypes[index];

    
    // Schritt 4: Farbe zuweisen
    float3 color;
    if (value == 0)
        color = float3(1, 0, 0); // rot
    else if (value == 1)
        color = float3(0, 1, 1);
    else if (value == 2)
        color = float3(0, 0, 1); // blau
    else if (value == 3)
        color = float3(1, 0, 1);
    else if (value == 4)
        color = float3(0, 0, 0);
    else if (value == 5)
        color = float3(1, 1, 1);
    else
        color = float3(0, 1, 0); // grün

    return float4(color, 1.0f);
    
}


float4 PS_rho(PSInput input) : SV_TARGET
{
    
    //uint gridWidth = 128;
    //uint gridHeight = 96;
    //uint gridDepth = 64;
    
    
    // Schritt 1: Texcoords Pixelkoordinaten
    uint x = uint(input.texcoord.x * gridWidth);
    uint y = uint(input.texcoord.y * gridHeight);
    uint z = debugPlanePos;
    // Schritt 2: Index berechnen
    uint index = z * gridWidth * gridHeight + y * gridWidth + x;

    // Schritt 3: Wert lesen
    LBCell3D grid = lbmCell[index];

    
    // Schritt 4: Farbe zuweisen
    float3 color = float3(grid.rho, 0.0f, grid.rho); // Blau  Rot
    
    return float4(color, 1.0f);
    
}

float4 PS_m(PSInput input) : SV_TARGET
{
    
    //uint gridWidth = 128;
    //uint gridHeight = 96;
    //uint gridDepth = 64;
    
    
    // Schritt 1: Texcoords Pixelkoordinaten
    uint x = uint(input.texcoord.x * gridWidth);
    uint y = uint(input.texcoord.y * gridHeight);
    uint z = debugPlanePos;
    // Schritt 2: Index berechnen
    uint index = z * gridWidth * gridHeight + y * gridWidth + x;

    // Schritt 3: Wert lesen
    LBCell3D grid = lbmCell[index];

    float red = 0.0f;
    
    // Schritt 4: Farbe zuweisen
    float piek = grid.m;
    if (grid.m < 0.0f)
    {
        float red = 1.0f;
    }
    else
        red = 0.0f;
    
    float normalizedM = saturate((grid.m - 1.0f) * 2.0f); // Annahme: m  1.0±0.5
    float3 color = float3(0.0f, grid.m, grid.m); // Blau  Rot
    
    return float4(color, 1.0f);
    
}

float4 PS_eps(PSInput input) : SV_TARGET
{
    
    //uint gridWidth = 128;
    //uint gridHeight = 96;
    //uint gridDepth = 64;
    
    
    // Schritt 1: Texcoords Pixelkoordinaten
    uint x = uint(input.texcoord.x * gridWidth);
    uint y = uint(input.texcoord.y * gridHeight);
    uint z = debugPlanePos;
    // Schritt 2: Index berechnen
    uint index = z * gridWidth * gridHeight + y * gridWidth + x;

    // Schritt 3: Wert lesen
    LBCell3D grid = lbmCell[index];

    
    // Schritt 4: Farbe zuweisen
    float normalizedM = saturate((grid.epsilon - 1.0f) * 2.0f); // Annahme: epsilon  1.0±0.5
    float3 color = float3(1.0f, normalizedM, 1.0f - normalizedM); // Blau  Rot
    
    return float4(color, 1.0f);
    
}