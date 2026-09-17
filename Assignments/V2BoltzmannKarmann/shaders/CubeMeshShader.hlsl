
#pragma shader_model 6_5

struct VertexOut
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

struct PrimitiveOut
{
    uint3 indices : SV_PrimitiveID; // Semantic Pflicht, auch wenn nicht benutzt
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

// 8 Eckpunkte eines Würfels im lokalen Raum (X, Y, Z)
static const float3 cubeVertices[8] =
{
    float3(-0.5, -0.5, -0.5),
    float3(-0.5, 0.5, -0.5),
    float3(0.5, 0.5, -0.5),
    float3(0.5, -0.5, -0.5),
    float3(-0.5, -0.5, 0.5),
    float3(-0.5, 0.5, 0.5),
    float3(0.5, 0.5, 0.5),
    float3(0.5, -0.5, 0.5),
};

// 12 Dreiecke (2 pro Fläche)
static const uint3 cubeTriangles[12] =
{
    uint3(0, 1, 2), uint3(0, 2, 3), // Rückseite
    uint3(4, 6, 5), uint3(4, 7, 6), // Vorderseite
    uint3(4, 5, 1), uint3(4, 1, 0), // Links
    uint3(3, 2, 6), uint3(3, 6, 7), // Rechts
    uint3(1, 5, 6), uint3(1, 6, 2), // Oben
    uint3(4, 0, 3), uint3(4, 3, 7) // Unten
};

static const float3 triangleColors[12] =
{
    float3(1, 0, 0), float3(0, 1, 0), // Rot, Grün
    float3(0, 0, 1), float3(1, 1, 0), // Blau, Gelb
    float3(1, 0, 1), float3(0, 1, 1), // Magenta, Cyan
    float3(0.5, 0.5, 0.5), float3(1, 0.5, 0), // Grau, Orange
    float3(0.5, 0, 1), float3(0, 0.5, 1), // Lila, Himmelblau
    float3(0.3, 0.8, 0.2), float3(0.9, 0.2, 0.4) // Limette, Pinkrot
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(
    uint3 threadId : SV_DispatchThreadID,
    out vertices VertexOut verts[8],
     out indices uint3 tris[12]
)
{
    SetMeshOutputCounts(8, 12);
    // 8 Würfel-Ecken (lokaler Raum)
    float3 cubeVertices[8] =
    {
        float3(-0.5, -0.5, -0.5),
        float3(-0.5, 0.5, -0.5),
        float3(0.5, 0.5, -0.5),
        float3(0.5, -0.5, -0.5),
        float3(-0.5, -0.5, 0.5),
        float3(-0.5, 0.5, 0.5),
        float3(0.5, 0.5, 0.5),
        float3(0.5, -0.5, 0.5)
    };

    // 12 Dreiecke (Index-Triple für jede Fläche)
    uint3 cubeTriangles[12] =
    {
        uint3(0, 1, 2), uint3(0, 2, 3), // Rückseite
        uint3(4, 6, 5), uint3(4, 7, 6), // Vorderseite
        uint3(4, 5, 1), uint3(4, 1, 0), // Links
        uint3(3, 2, 6), uint3(3, 6, 7), // Rechts
        uint3(1, 5, 6), uint3(1, 6, 2), // Oben
        uint3(4, 0, 3), uint3(4, 3, 7) // Unten
    };

    // Transformiere Vertices in Clip Space
    for (uint i = 0; i < 8; ++i)
    {
        float4 localPos = float4(cubeVertices[i], 1.0);
        verts[i].position = mul(projectionMatrix, mul(cameraMatrix, localPos));
    }
    
    for (uint i = 0; i < 12; ++i)
    {
        uint3 tri = cubeTriangles[i];
        float3 color = triangleColors[i];

        verts[tri.x].color = color;
        verts[tri.y].color = color;
        verts[tri.z].color = color;

        tris[i] = tri;
    }

    // Kopiere Indizes
    for (uint i = 0; i < 12; ++i)
    {
        tris[i] = cubeTriangles[i];
    }
    
}

float4 PSMain(VertexOut input) : SV_Target
{
    return float4(input.color, 1.0); // Orange
    //return float4(projectionMatrix[1]);
}