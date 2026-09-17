#pragma shader_model 6_5

struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR;
};
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
};

cbuffer PerMeshConstants : register(b1)
{
    float4x4 cameraMatrix;
};


StructuredBuffer<uint> edgeTable : register(t0);
StructuredBuffer<int> triTable : register(t1);

StructuredBuffer<LBCell3D> grid : register(t2); // LBM-Buffer 


[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    uint3 threadId : SV_DispatchThreadID,
    out vertices VertexOut verts[15],
    out indices uint3 tris[5]
)
{
    //DEBUG
    
    float rho_test = grid[threadId.x].rho;
    float dummy = rho_test * 0.00001;
    
    float3 origin = { 0.0f, 0.0f, 0.0f }; // Position im Raum (Zentrierung)
    float voxelSize = blurStrength; // Würfelgröße = 1 Einheit

    
    float rhoBase = 1.0f; // Basisdichte (Isowert = 1.0f)
    float rhoVariation = 0.5f; // Variation erzeugt Übergänge
    uint3 cell = threadId;
    float3 cellPos = origin + voxelSize * float3(cell);

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
  
    
// Kugeldefinition
    float3 sphereCenter = origin + float3(4.0, 4.0, 4.0); // Mitte eines 8x8 Grid
    float sphereRadius = voxelSize * 3.0f;

    float cornerValue[8];
    for (int i = 0; i < 8; ++i)
    {
        float3 delta = cornerPos[i] - sphereCenter;
        float dist2 = dot(delta, delta);
        cornerValue[i] = sphereRadius * sphereRadius - dist2;
    }
    
    float isoValue = 0.0f;
    uint caseIndex = 0;
    for (uint i = 0; i < 8; ++i)
    {
        if (cornerValue[i] > isoValue)
            caseIndex |= (1u << i);
    }

    uint edgeMask = edgeTable[caseIndex];
    
        /*if (edgeMask == 0)
    {
        SetMeshOutputCounts(0, 0);
        return;
    }
*/
    // --- Kanten interpolieren
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

            float t = abs(denom) < 1e-5 ? 0.5f : saturate((isoValue - v1) / denom);
            
            // Fallback bei NaN
            if (!isfinite(t))
                t = 0.5f;
        
            edgeVertex[i] = lerp(p1, p2, t);
        }
    }

    // --- Zählen der gültigen Dreiecke
    uint validTris = 0;
    uint validVerts = 0;

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

        if (!isfinite(pa.x) || !isfinite(pb.x) || !isfinite(pc.x))
            continue;

        validTris++;
        validVerts += 3;
    }

    SetMeshOutputCounts(validVerts, validTris);

    if (validVerts == 0)
        return;

    // --- Ausgabe schreiben
    uint vIndex = 0;
    uint tIndex = 0;

    for (uint i = 0; i < 5; ++i)
    {
        int a = triTable[caseIndex * 16 + i * 3 + 0];
        int b = triTable[caseIndex * 16 + i * 3 + 1];
        int c = triTable[caseIndex * 16 + i * 3 + 2];

        if (a == -1 || b == -1 || c == -1)
            break;

        /*
        if ((edgeMask & (1 << a)) == 0 ||
        (edgeMask & (1 << b)) == 0 ||
        (edgeMask & (1 << c)) == 0)
            continue;
*/        

        float3 pa = edgeVertex[a];
        float3 pb = edgeVertex[b];
        float3 pc = edgeVertex[c];

        if (!isfinite(pa.x) || !isfinite(pb.x) || !isfinite(pc.x))
            continue;

        verts[vIndex + 0].position = mul(projectionMatrix, mul(cameraMatrix, float4(pa, 1.0f)));
        verts[vIndex + 1].position = mul(projectionMatrix, mul(cameraMatrix, float4(pb, 1.0f)));
        verts[vIndex + 2].position = mul(projectionMatrix, mul(cameraMatrix, float4(pc, 1.0f)));

        float3 debugColor = float3(
            threadId.x / 8.0f,
            threadId.y / 8.0f,
            threadId.z / 8.0f
        );
        
        /*
        // ### DEBUG TRIANGLE COLOR
        float minBound = origin.x;
        float maxBound = origin.x + voxelSize * 8.0f;
        
        float3 debugColor = float3(0.0f, 1.0f, 0.0f); // grün = normal

        if (!isfinite(pa.x) || !isfinite(pb.x) || !isfinite(pc.x))
        {
            debugColor = float3(1.0f, 0.0f, 0.0f); // rot = NaN
        }
        else if (any(pa < minBound) || any(pa > maxBound) ||
    any(pb < minBound) || any(pb > maxBound) ||
    any(pc < minBound) || any(pc > maxBound))
        {
            debugColor = float3(1, 1, 0); // Gelb = außerhalb Grid
        }
        // END TRIANGEL DEBUG COLOR ###
*/
        
        
        verts[vIndex + 0].color = debugColor * rho_test;
        verts[vIndex + 1].color = debugColor;
        verts[vIndex + 2].color = debugColor;

        tris[tIndex] = uint3(vIndex, vIndex + 1, vIndex + 2);

        vIndex += 3;
        tIndex++;
    }
}

float4 PSMain(VertexOut input) : SV_Target
{
    return float4(input.color, 1.0);
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

        lines[i] = uint2(i * 2, i * 2 + 1);
    }

}