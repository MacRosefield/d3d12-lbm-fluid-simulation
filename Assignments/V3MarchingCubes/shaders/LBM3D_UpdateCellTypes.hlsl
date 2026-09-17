// LBM3D_UpdateCellTypes.hlsl

#include "LBM3DCommon.hlsl"



RWStructuredBuffer<LBCell3D> b_gridData : register(u0);
RWStructuredBuffer<uint> b_cellTypes : register(u1);


static const float kappa = 1e-3f;



[numthreads(8, 8, 8)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    
   
   // uint gridHeight = 96;
    //uint gridDepth = 64;
    
    uint x = threadID.x;
    uint y = threadID.y;
    uint z = threadID.z;

    if (x >= gridWidth || y >= gridHeight || z >= gridDepth)
        return;

    uint index = x + y * gridWidth + z * gridWidth * gridHeight;
    LBCell3D cell = b_gridData[index];

    /*
    float thresholdHigh = (1.0 + kappa) * cell.rho;
    float thresholdLow = (0.0 - kappa) * cell.rho;
*/
    uint oldType = b_cellTypes[index];
    float thresholdHigh = 15.0f;
    float thresholdLow = 0.001f;
    
    if (oldType == Cell_Wall || oldType == Cell_Inflow)
        return;
  

    
    if (cell.m >= thresholdHigh)
    {
        b_cellTypes[index] = Cell_Fluid;
    }
    else if (cell.m < thresholdLow)
    {
        b_cellTypes[index] = Cell_Empty;
    }
    else
    {
        b_cellTypes[index] = Cell_Interface;
    }

}
