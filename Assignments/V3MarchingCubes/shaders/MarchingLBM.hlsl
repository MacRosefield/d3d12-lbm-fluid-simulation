#pragma shader_model 6_5


#include "LBM3DCommon.hlsl"


struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR;
    float3 worldPos : POSITION0;
};



cbuffer PerMeshConstants : register(b1)
{
    float4x4 cameraMatrix;
};


StructuredBuffer<uint> edgeTable : register(t0);
StructuredBuffer<int> triTable : register(t1);

StructuredBuffer<uint> cellType : register(t2); // LBM-Buffer 

RWStructuredBuffer<uint> gTriCount : register(u5);


// vor der main-Funktion (oder ganz oben im Shader) definieren:
static const int3 cornerOffset[8] =
{
    int3(0, 0, 0),
    int3(1, 0, 0),
    int3(1, 1, 0),
    int3(0, 1, 0),
    int3(0, 0, 1),
    int3(1, 0, 1),
    int3(1, 1, 1),
    int3(0, 1, 1)
};


[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
  
    uint3 threadId : SV_DispatchThreadID,
    
    out vertices VertexOut verts[15],
    out indices uint3 tris[5]
)
{
    
    //float3 cellPos = float3(threadId);
    float3 cellPos = float3(threadId);
    
    const int3 gridSize = int3(gridWidth, gridHeight, gridDepth);
    const uint index = threadId.x + threadId.y * gridSize.x + threadId.z * gridSize.x * gridSize.y;

    float voxelSize = 1.0f;
    

    float3 cornerPos[8] =
    {
        cellPos,
        cellPos + float3(voxelSize, 0, 0),
        cellPos + float3(voxelSize, voxelSize, 0),
        cellPos + float3(0, voxelSize, 0),
        cellPos + float3(0, 0, voxelSize),
        cellPos + float3(voxelSize, 0, voxelSize),
        cellPos + float3(voxelSize, voxelSize, voxelSize),
        cellPos + float3(0, voxelSize, voxelSize)
    };
    
    
    
    //  1. Zell-Ecken und Typen erfassen
    //float3 cornerPos[8];
    uint cornerType[8];
    float cornerValue[8];

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        int3 pos = int3(threadId) + cornerOffset[i];
        uint idx = pos.x + pos.y * gridSize.x + pos.z * gridSize.x * gridSize.y;

        //cornerPos[i] = float3(pos);
        cornerType[i] = cellType[idx];
        cornerValue[i] = float(cornerType[i]);
    }

    //  2. Pr�fen, ob alle Typen gleich kein Mesh erzeugen
    bool sameType = true;
    for (int i = 1; i < 8; ++i)
    {
        if (cornerType[i] != cornerType[0])
        {
            sameType = false;
            break;
        }
    }

    //  3. Nur Mesh, wenn Typwechsel vorhanden UND caseIndex g�ltig
    uint caseIndex = 0;
    uint edgeMask = 0;
    if (!sameType)
    {
        [unroll]
        for (uint i = 0; i < 8; ++i)
        {
            caseIndex |= (uint(cornerValue[i] > 0.5f) << i);
        }

        edgeMask = edgeTable[caseIndex];
    }

    //  4. Triangles z�hlen (aber noch NICHT ausgeben)
    uint numVerts = 0;
    uint numTris = 0;

    if (edgeMask != 0)
    {
        for (uint i = 0; i < 5; ++i)
        {
            int a = triTable[caseIndex * 16 + i * 3 + 0];
            int b = triTable[caseIndex * 16 + i * 3 + 1];
            int c = triTable[caseIndex * 16 + i * 3 + 2];
            if (a == -1 || b == -1 || c == -1)
                break;

            numTris++;
            numVerts += 3;
        }
    }
    
    if (threadId.x >= gridSize.x - 1 ||
        threadId.y >= gridSize.y - 1 ||
        threadId.z >= gridSize.z - 1 ||
        threadId.x == 0 || threadId.y == 0 || threadId.z == 0)
    {
        numTris = 0;
        numVerts = 0;
    }

  
    InterlockedAdd(gTriCount[0], numTris);
   

    //  5. EINZIGER g�ltiger Aufruf:
    SetMeshOutputCounts(numVerts, numTris);

    if (numVerts == 0)
        return;

    //  6. Kanten interpolieren
    static const uint2 edgeToCorner[12] =
    {
        uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
        uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
        uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
    };

    float3 edgeVertex[12];
    for (uint i = 0; i < 12; ++i)
    {
        if ((edgeMask & (1u << i)) != 0)
        {
            uint2 c = edgeToCorner[i];
            float3 p1 = cornerPos[c.x];
            float3 p2 = cornerPos[c.y];
            float v1 = cornerValue[c.x];
            float v2 = cornerValue[c.y];
            float denom = v2 - v1;

            float t = abs(denom) < 1e-5 ? 0.5f : saturate((1.0f - v1) / denom);
            if (!isfinite(t))
                t = 0.5f;

            edgeVertex[i] = lerp(p1, p2, t);
        }
    }

    //  7. Dominanten Typ f�r Farbe bestimmen
    uint histogram[5] = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < 8; ++i)
    {
        uint type = cornerType[i];
        
        if (type == 0)
            continue;
        
        if (type < 5)
            histogram[type]++;
    }

    uint dominantType = 0;
    uint maxCount = 0;
    for (uint t = 0; t < 5; ++t)
    {
        if (histogram[t] > maxCount)
        {
            maxCount = histogram[t];
            dominantType = t;
        }
    }

    float3 debugColor;
    switch (dominantType)
    {
        case 0:
            debugColor = float3(1, 1, 1);
            break; // CELL_EMPTY
        case 1:
            debugColor = float3(0, 0, 1);
            break; // CELL_WALL
        case 2:
            debugColor = float3(0, 1, 0);
            break; // CELL_FLUID
        case 3:
            debugColor = float3(1, 0, 0);
            break; // CELL_INFLOW
        case 4:
            debugColor = float3(1, 1, 0);
            break; // CELL_OUTFLOW
        default:
            debugColor = float3(1, 0, 1);
            break; // Unknown
    }

    //  8. Ausgabe
    uint vIndex = 0;
    uint tIndex = 0;

    for (uint i = 0; i < 5; ++i)
    {
        int a = triTable[caseIndex * 16 + i * 3 + 0];
        int b = triTable[caseIndex * 16 + i * 3 + 1];
        int c = triTable[caseIndex * 16 + i * 3 + 2];
        if (a == -1 || b == -1 || c == -1)
            break;

        float3 pa = edgeVertex[a];
        float3 pb = edgeVertex[b];
        float3 pc = edgeVertex[c];


        verts[vIndex + 0].position = mul(projectionMatrix, mul(cameraMatrix, float4(pa, 1.0f)));
        verts[vIndex + 1].position = mul(projectionMatrix, mul(cameraMatrix, float4(pb, 1.0f)));
        verts[vIndex + 2].position = mul(projectionMatrix, mul(cameraMatrix, float4(pc, 1.0f)));

        verts[vIndex + 0].worldPos = mul(cameraMatrix, float4(pa, 1.0f));
        verts[vIndex + 1].worldPos = mul(cameraMatrix, float4(pb, 1.0f));
        verts[vIndex + 2].worldPos = mul(cameraMatrix, float4(pc, 1.0f));

        
        verts[vIndex + 0].color = debugColor;
        verts[vIndex + 1].color = debugColor;
        verts[vIndex + 2].color = debugColor;

        tris[tIndex] = uint3(vIndex, vIndex + 1, vIndex + 2);
        vIndex += 3;
        tIndex++;
    }
}

float4 PSMain(VertexOut input) : SV_Target
{
    //return float4(input.color, 1.0);
    float3 cameraNormal = normalize(cross(ddy(input.worldPos), ddx(input.worldPos)));
    
    //return float4(cameraNormal.zzz, 1);
    return float4(input.color * cameraNormal.zzz, 1);
    

}

[outputtopology("line")]
[numthreads(1, 1, 1)]
void main_debug(
    uint3 threadId : SV_DispatchThreadID,
    out vertices VertexOut verts[24],
    out indices uint2 lines[12]
)
{
    SetMeshOutputCounts(24, 12);

    float3 origin = { 0.5f, 0.5f, 0.5f };
    float voxelSize = 1.0f;
    float3 base = origin + voxelSize * float3(threadId);

    static const float3 cubeCorners[8] =
    {
        float3(0, 0, 0), float3(1, 0, 0), float3(1, 1, 0), float3(0, 1, 0),
        float3(0, 0, 1), float3(1, 0, 1), float3(1, 1, 1), float3(0, 1, 1)
    };

    static const uint2 cubeEdges[12] =
    {
        uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
        uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
        uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
    };

    for (int i = 0; i < 12; ++i)
    {
         
        float3 p0 = base + voxelSize * cubeCorners[cubeEdges[i].x];
        float3 p1 = base + voxelSize * cubeCorners[cubeEdges[i].y];

        verts[i * 2 + 0].position = mul(projectionMatrix, mul(cameraMatrix, float4(p0, 1.0f)));
        verts[i * 2 + 1].position = mul(projectionMatrix, mul(cameraMatrix, float4(p1, 1.0f)));

        verts[i * 2 + 0].color = float3(0.2f, 0.2f, 0.2f);
        verts[i * 2 + 1].color = float3(0.2f, 0.2f, 0.2f);
        
        verts[i * 2 + 0].worldPos = 0;
        verts[i * 2 + 1].worldPos = 0;

        lines[i] = uint2(i * 2, i * 2 + 1);
    }
}