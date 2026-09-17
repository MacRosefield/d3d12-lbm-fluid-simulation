// LBM3D_Common.hlsl

#ifndef LBM3D_COMMON
#define LBM3D_COMMON

// --- Structs ---
struct LBCell3D
{
    float rho;
    float3 u;
    float f[19];
    float m;
    float epsilon;
};

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

// --- Constants ---
static const int3 c[19] =
{
    int3(0, 0, 0),
    int3(1, 0, 0), int3(-1, 0, 0),
    int3(0, 1, 0), int3(0, -1, 0),
    int3(0, 0, 1), int3(0, 0, -1),
    int3(1, 1, 0), int3(-1, 1, 0), int3(1, -1, 0), int3(-1, -1, 0),
    int3(1, 0, 1), int3(-1, 0, 1), int3(1, 0, -1), int3(-1, 0, -1),
    int3(0, 1, 1), int3(0, -1, 1), int3(0, 1, -1), int3(0, -1, -1)
};

static const float w[19] =
{
    1.0 / 3.0,
    1.0 / 18.0, 1.0 / 18.0,
    1.0 / 18.0, 1.0 / 18.0,
    1.0 / 18.0, 1.0 / 18.0,
    1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0,
    1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0,
    1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0, 1.0 / 36.0
};

float computeEquilibrium(int i, float rho, float3 u)
{
    float cu = dot((float3) c[i], u);
    float u2 = dot(u, u);
    return w[i] * rho * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * u2);
}


static const int oppositeOf[19] =
{
    0, //  0 <-> 0
    2, //  1 <-> 2
    1, //  2 <-> 1
    4, //  3 <-> 4
    3, //  4 <-> 3
    6, //  5 <-> 6
    5, //  6 <-> 5
   10, //  7 <-> 10
    9, //  8 <-> 9
    8, //  9 <-> 8
    7, // 10 <-> 7
   14, // 11 <-> 14
   13, // 12 <-> 13
   12, // 13 <-> 12
   11, // 14 <-> 11
   18, // 15 <-> 18
   17, // 16 <-> 17
   16, // 17 <-> 16
   15 // 18 <-> 15
};

// --- Cell Types ---
static const uint Cell_Empty = 0;
static const uint Cell_Interface = 1;
static const uint Cell_Fluid = 2;
static const uint Cell_Obstacle = 3;
static const uint Cell_Wall = 4;
static const uint Cell_Inflow = 5;
static const uint Cell_Outflow = 6;


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
#endif
