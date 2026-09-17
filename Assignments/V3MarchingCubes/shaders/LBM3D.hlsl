
// LBM3D.hlsl

#include "LBM3DCommon.hlsl"


RWStructuredBuffer<LBCell3D> b_gridRead : register(u0);
RWStructuredBuffer<LBCell3D> b_gridWrite : register(u1);
RWStructuredBuffer<uint> b_cellTypes : register(u3);

/*
float computeEquilibrium(int i, float rho, float3 u)
{
    float cu = dot((float3) c[i], u);
    float u2 = dot(u, u);
    return w[i] * rho * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * u2);
}
*/
float reconstructDF(int i, float rhoA, float3 u, float fi)
{
    float feq_i = computeEquilibrium(i, rhoA, u);
    float feq_op = computeEquilibrium(oppositeOf[i], rhoA, u);
    return feq_i + feq_op - fi;
}

[numthreads(8, 8, 8)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    
    float tau = b_tau;
    
    float omega = 1.0f / tau;
    float cs2 = 1.0f / 3.0f;
    
    float3 g = float3(0.00f, -0.10f, 0.0f); // skalierte Schwerkraft
    
    float rho = 0.0f;
    float3 u = float3(0, 0, 0);
    
    
    uint x = threadID.x;
    uint y = threadID.y;
    uint z = threadID.z;

    if (x >= gridWidth || y >= gridHeight || z >= gridDepth)
        return;

    uint index = x + y * gridWidth + z * gridWidth * gridHeight;
       
    uint type = b_cellTypes[index];
    LBCell3D cell = b_gridRead[index];
    
   
    // INFLOW ZELLEN
    if (type == Cell_Inflow)
    {
        float rhoIn = inflow_u;
        //float3 uIn = float3(0.0f, -0.50f, 0.0f); // Inflow-Geschwindigkeit nach unten
        
        
        float3 uIn = float3(0.0f, -(inflow_u), 0.0f);
        
        LBCell3D inflowCell;
        
        inflowCell.rho = rhoIn;
        inflowCell.u = uIn;
        inflowCell.m = rhoIn;
        inflowCell.epsilon = 1.0f;

        float u2 = dot(uIn, uIn);
        
        for (int i = 0; i < 19; ++i)
        {
            float cu = dot(c[i], uIn);
            
            inflowCell.f[i] = w[i] * rhoIn * (1 + 3 * cu + 4.5f * cu * cu - 1.5f * u2);
        }

        b_gridWrite[index] = inflowCell;
        return;
    }
    
    if (type == Cell_Outflow)
    {
        float rho = outflow_u;
        float3 u = float3(0, -outflow_u, 0);

        for (int i = 0; i < 19; ++i)
        {
            rho += cell.f[i];
            u += cell.f[i] * (float3) c[i];
        }

        
        u = (rho > 1e-5f) ? u / rho : float3(0, 0, 0);

        cell.rho = rho;
        cell.u = u;
        b_gridWrite[index] = cell;
        return;
    }
     /*
    if (type == Cell_Wall || type == Cell_Obstacle)
    {
        for (int i = 0; i < 19; ++i)
        {
            int opp = oppositeOf[i];
            cell.f[i] = b_gridRead[index].f[opp];
        }
        cell.rho = 0.0f;
        cell.u = float3(0, 0, 0);
        cell.m = 0.0f;
        cell.epsilon = 0.0f;
        b_gridWrite[index] = cell;
        return;
    }
   */
  
    /*
     // === INTERFACE: Rekonstruktion von DFs ===
    if (type == Cell_Interface)
    {
        for (int i = 0; i < 19; ++i)
        {
            int3 npos = int3(x, y, z) + c[i];
            if (npos.x < 0 || npos.y < 0 || npos.z < 0 ||
                npos.x >= gridWidth || npos.y >= gridHeight || npos.z >= gridDepth)
                continue;

            uint nidx = npos.x + npos.y * gridWidth + npos.z * gridWidth * gridHeight;
            uint ntype = b_cellTypes[nidx];

            if (ntype == Cell_Empty)
            {
                cell.f[i] = reconstructDF(i, 1.0f, cell.u, cell.f[i]);
            }
        }
    }
     */ 
    
    
    
    
    
    // === Kollision mit Guo-Kraftterm ===
    for (int i = 0; i < 19; ++i)
    {
        rho += cell.f[i];
        u += cell.f[i] * (float3) c[i];
    }

    u = (rho > 1e-5f) ? u / rho : float3(0, 0, 0);

    float3 F = rho * g; // g: extern definierte Gewichtskraft (float3)

    float u2 = dot(u, u);

    for (int i = 0; i < 19; ++i)
    {
        float3 ci = (float3) c[i];
        float cu = dot(ci, u);

        float feq = w[i] * rho * (1.0f + 3.0f * cu + 4.5f * cu * cu - 1.5f * u2);

        // === Guo Kraftterm ===
        float Fi = w[i] * (
            (dot(ci, F) / cs2) +
            (3.0f * dot(ci, u) * dot(ci, F)) / (cs2 * cs2) -
            (dot(u, F) / cs2)
        );

        // Relaxation mit Guo-Kraft
        cell.f[i] += omega * (feq - cell.f[i]) + (1.0f - 0.5f * omega) * Fi;
        cell.f[i] = clamp(cell.f[i], 0.0f, 100.0f);
    }

    cell.rho = rho;
    cell.u = u;
    cell.m = 0.5f * rho * dot(u, u);
    b_gridWrite[index] = cell;
    /*
*/
}
    
    // === INFLOW ===


    // === OUTFLOW ===


    // === OBSTACLE ===


    // === FLUID ===

    // === KOLLISION ===

