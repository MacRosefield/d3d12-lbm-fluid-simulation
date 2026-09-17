// MarchingCubes.hlsl
#pragma shader_model 6_5

struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

struct PrimitiveOut
{
    uint3 indices : SV_PrimitiveID;
};

cbuffer PerFrameConstants : register(b0)
{
    float4x4 projectionMatrix;
    float3 cameraPosition;
}

cbuffer PerMeshConstants : register(b1)
{
    float4x4 cameraMatrix;
}

StructuredBuffer<uint> edgeTable : register(t0);
StructuredBuffer<int> triTable : register(t1);

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    uint3 threadId : SV_DispatchThreadID,
    out vertices VertexOut verts[15],
    out indices uint3 tris[5]
)
{
    SetMeshOutputCounts(15, 5); // früh aufgerufen, vor jedem Vertex/Index-Zugriff

    float isoValue = 1.0f;
    float3 cornerPos[8] =
    {
        float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5),
        float3(0.5, 0.5, -0.5), float3(-0.5, 0.5, -0.5),
        float3(-0.5, -0.5, 0.5), float3(0.5, -0.5, 0.5),
        float3(0.5, 0.5, 0.5), float3(-0.5, 0.5, 0.5)
    };

    float cornerValue[8] = { 0.8f, 1.1f, 1.2f, 0.9f, 1.0f, 1.05f, 0.95f, 1.15f };

    uint caseIndex = 0;
    for (uint i = 0; i < 8; ++i)
    {
        if (cornerValue[i] > isoValue)
            caseIndex |= (1u << i); // bereinigt: 1u statt 1
    }

    uint edgeMask = edgeTable[caseIndex];

    static const uint2 edgeToCorner[12] =
    {
        uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
        uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
        uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
    };

    float3 edgeVertex[12];
    for (uint i = 0; i < 12; ++i)
    {
        if ((edgeMask & (1u << i)) != 0) // bereinigt: 1u statt 1
        {
            uint2 c = edgeToCorner[i];
            float3 p1 = cornerPos[c.x];
            float3 p2 = cornerPos[c.y];
            float v1 = cornerValue[c.x];
            float v2 = cornerValue[c.y];
            float t = saturate((isoValue - v1) / (v2 - v1));
            edgeVertex[i] = lerp(p1, p2, t);
        }
    }

    uint vertCount = 0;
    uint triCount = 0;

    for (uint i = 0; i < 5; ++i)
    {
        int a = triTable[caseIndex * 16 + i * 3 + 0];
        int b = triTable[caseIndex * 16 + i * 3 + 1];
        int c = triTable[caseIndex * 16 + i * 3 + 2];

        if (a == -1 || b == -1 || c == -1)
            break;

        verts[vertCount].position = mul(projectionMatrix, mul(cameraMatrix, float4(edgeVertex[a], 1.0f)));
        verts[vertCount].color = float3(1, 0, 0);

        verts[vertCount + 1].position = mul(projectionMatrix, mul(cameraMatrix, float4(edgeVertex[b], 1.0f)));
        verts[vertCount + 1].color = float3(0, 1, 0);

        verts[vertCount + 2].position = mul(projectionMatrix, mul(cameraMatrix, float4(edgeVertex[c], 1.0f)));
        verts[vertCount + 2].color = float3(0, 0, 1);

        tris[triCount] = uint3(vertCount, vertCount + 1, vertCount + 2);

        vertCount += 3;
        triCount++;
    }
}

float4 PSMain(VertexOut input) : SV_Target
{
    return float4(input.color, 1.0);
}
