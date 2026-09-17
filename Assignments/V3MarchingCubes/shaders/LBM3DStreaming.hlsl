
// LBM3DStreaming.hlsl

#include "LBM3DCommon.hlsl"




RWStructuredBuffer<LBCell3D> b_gridRead : register(u0);
RWStructuredBuffer<LBCell3D> b_gridWrite : register(u1);
RWStructuredBuffer<uint> b_cellTypes : register(u3);



[numthreads(8, 8, 8)]
void main(uint3 threadID : SV_DispatchThreadID)
{

    uint x = threadID.x;
    uint y = threadID.y;
    uint z = threadID.z;

    if (x >= gridWidth || y >= gridHeight || z >= gridDepth)
        return;

    uint dstIndex = x + y * gridWidth + z * gridWidth * gridHeight;
    uint type = b_cellTypes[dstIndex];

    LBCell3D result;
    
    result.rho = 0.0f;
    result.u = float3(0.0f, 0.0f, 0.0f);
    result.m = 0.0f;
    result.epsilon = 0.0f;

    for (int i = 0; i < 19; ++i)
        result.f[i] = 0.0f;
    
    // INFLOW: skip, wird separat gesetzt
    if (type == Cell_Inflow)
        return;
/*
    // OUTFLOW: kopiere wie sie sind
    if (type == Cell_Outflow)
    {
        b_gridWrite[dstIndex] = b_gridRead[dstIndex];
        return;
    }
 */
    // BOUNCE BACK für WALL / OBSTACLE
    if (type == Cell_Wall || type == Cell_Obstacle)
    {
        for (int i = 0; i < 19; ++i)
        {
            int opp = oppositeOf[i];
            result.f[i] = b_gridRead[dstIndex].f[opp];
        }
        result.rho = 0.0f;
        result.u = float3(0, 0, 0);
        result.m = 0.0f;
        result.epsilon = 0.0f;

        b_gridWrite[dstIndex] = result;
        return;
    }

   
    float massDelta = 0.0f;
    for (int i = 0; i < 19; ++i)
    {
        int3 offset = c[i];
        int sx = int(x) - offset.x;
        int sy = int(y) - offset.y;
        int sz = int(z) - offset.z;

        if (sx >= 0 && sx < gridWidth &&
            sy >= 0 && sy < gridHeight &&
            sz >= 0 && sz < gridDepth)
        {
            uint srcIndex = sx + sy * gridWidth + sz * gridWidth * gridHeight;
            float f_in = b_gridRead[srcIndex].f[i];
            float f_out = b_gridRead[dstIndex].f[i];
            float eps_src = b_gridRead[srcIndex].epsilon;
            float eps_dst = b_gridRead[dstIndex].epsilon;
            float areaFactor = 0.5f * (eps_src + eps_dst);
            //float areaFactor = colorIntens;
            
            result.f[i] = f_in;
            massDelta += (f_in - f_out) * areaFactor;
            

        }
        else
        {
            result.f[i] = 0.0f; // keine Beiträge von außen
        }
    }

    // Testweise fi aufmultiplizieren
    result.m = b_gridRead[dstIndex].m + massDelta;
    /*
    float m = 0.0f;
    for (int i = 0; i < 19; ++i)
        m += result.f[i];
    result.m = m;
    */
    float rho = 0.0f;
    float3 u = float3(0, 0, 0);

    for (int i = 0; i < 19; ++i)
    {
        rho += result.f[i];
        u += result.f[i] * (float3) c[i];
    }
    result.rho = rho;
    result.u = (rho > 1e-5f) ? u / rho : float3(0, 0, 0);
    /*
    result.rho = b_gridRead[dstIndex].rho;
    result.u = b_gridRead[dstIndex].u;
*/
    float epsilon = 0.0f;
    if (isfinite(result.m) && isfinite(result.rho) && result.rho > 1e-5f)
    {
        epsilon = result.m / result.rho;
        if (!isfinite(epsilon) || epsilon > 1e6f)
            epsilon = 0.0f;
    }
    result.epsilon = epsilon;

    b_gridWrite[dstIndex] = result;
}
