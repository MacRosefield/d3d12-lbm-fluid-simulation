// LBM.hlsl

struct LBCell
{
    float rho; // Dichte
    float2 u; // Geschwindigkeit (x, y)
    float f[9]; // Verteilungsfunktionen für D2Q9
};

// === Ressourcen ===
RWStructuredBuffer<LBCell> b_gridRead : register(u0); // Eingabe-Buffer
RWStructuredBuffer<LBCell> b_gridWrite : register(u1); // Ziel-Buffer nach Collision
RWTexture2D<float4> g_OutputTexture : register(u2); // Ausgabe-Textur (Visualisierung)
RWStructuredBuffer<uint> b_cellTypes : register(u3); // Buffer für celltypen für waende obstacles etc

struct Light
{
    float3 position;
    float pad1;
    float3 color;
    float intensity;
};

cbuffer FogValues : register(b0)
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
};

// === Richtungsvektoren (D2Q9) ===
static const int2 c[9] =
{
    int2(0, 0), // 0
    int2(1, 0), // 1: rechts
    int2(0, 1), // 2: oben
    int2(-1, 0), // 3: links
    int2(0, -1), // 4: unten
    int2(1, 1), // 5: rechts oben
    int2(-1, 1), // 6: links oben
    int2(-1, -1), // 7: links unten
    int2(1, -1) // 8: rechts unten
};

// === Gewichtsfaktoren (D2Q9) ===
static const float w[9] =
{
    4.0 / 9.0,
    1.0 / 9.0, 1.0 / 9.0,
    1.0 / 9.0, 1.0 / 9.0,
    1.0 / 36.0, 1.0 / 36.0,
    1.0 / 36.0, 1.0 / 36.0
};

static const int oppositeOf[9] = { 0, 3, 4, 1, 2, 7, 8, 5, 6 };

// Muss exakt mit C++ enum übereinstimmen
static const uint Cell_Fluid = 0;
static const uint Cell_Inflow = 1;
static const uint Cell_Outflow = 2;
static const uint Cell_Obstacle = 3;
static const uint Cell_Wall = 4;


float computeEquilibrium(int i, float rho, float2 u)
{
    float cu = float(c[i].x) * u.x + float(c[i].y) * u.y;
    float uSq = dot(u, u);

    return w[i] * rho * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * uSq);
}

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint gridWidth = 640;
    uint gridHeight = 480;

    uint x = threadID.x;
    uint y = threadID.y;
    uint index = y * gridWidth + x;

    if (x >= gridWidth || y >= gridHeight)
        return;

    LBCell cell = b_gridRead[index];
    uint cellType = b_cellTypes[index];
    
    // === Inflow-Zellen: Geschwindigkeit erzwingen ===
    if (cellType == 1)
    {
    
        float yNorm = float(y) / float(gridHeight - 160);
        //float uMax = 0.25f;
        float uMax = inflow_u;
        float ux = uMax * (1.0 - pow(2.0 * yNorm - 1.0, 2.0));
        float2 u_in = float2(ux, 0.0f);
        float rho_in = 1.0f;

        LBCell result;
        for (int i = 0; i < 9; ++i)
        {
            result.f[i] = computeEquilibrium(i, rho_in, u_in);
        }

        result.rho = rho_in;
        result.u = u_in;

        b_gridWrite[threadID.y * gridWidth + threadID.x] = result;
        
        return;
    }

    if (cellType == 2) // OUTFLOW
    {
        LBCell result = b_gridRead[index];

    // Schritt 1: Dichte und Geschwindigkeit berechnen (aus lokalen f[i])
        float rho = 0.0f;
        float2 u = float2(0.0f, 0.0f);
        for (int i = 0; i < 9; ++i)
        {
            rho += result.f[i];
            u += result.f[i] * c[i];
        }

    // Schutz vor Instabilität
        rho = clamp(rho, 0.01f, 2.0f);
        u = (rho > 1e-5f) ? (u / rho) : float2(0.0f, 0.0f);

        result.rho = rho;
        result.u = u;
        //result.u = float2(outflow_u, 0.0f); // Optional: feste Zielgeschwindigkeit

       // Kein Kollisionsschritt f[i] bleibt unangetastet
        b_gridWrite[index] = result;
        return;
    }
    
    if (cellType == 3) // OBSTACLE
    {
    // Bounce-back innerhalb der Zelle
        for (int i = 0; i < 9; ++i)
        {
            int opp = oppositeOf[i];
            cell.f[i] = b_gridRead[threadID.y * gridWidth + threadID.x].f[opp];
        }

        cell.rho = 1.0f;
        cell.u = float2(0.0f, 0.0f);

        b_gridWrite[threadID.y * gridWidth + threadID.x] = cell;
        return;
    }
         
    // === Standardzellen: Fluid oder Outflow ==
    // Schritt 1: Dichte Geschwindigkeit berechnen
    float rho = 0.0f;
    float2 u = float2(0.0f, 0.0f);

    for (int i = 0; i < 9; i++)
    {
        rho += cell.f[i];
        u += cell.f[i] * c[i];
    }

    if (rho > 1e-5f)
        u /= rho;
    else
        u = float2(0.0f, 0.0f);

    cell.rho = rho;
    cell.u = u;

    //Schritt 2: LBGK-Kollision
    
    float feq[9];
    float u2 = dot(u, u);
    float tau = 2.2f;

    for (int i = 0; i < 9; i++)
    {
        float cu = dot(c[i], u);
        feq[i] = w[i] * rho * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * u2);
        cell.f[i] += (feq[i] - cell.f[i]) / tau;
        //DEBUG clamp
        cell.f[i] = clamp(cell.f[i], 0.0f, 100.0f);
    }
    
    // Speichern
    b_gridWrite[index] = cell;

    // === Visualisierung
    // Variante 1 - Strömungsdarstellung
    //float uScale = 5.0f;
    /*
    
    float uScale = colorIntens;
    g_OutputTexture[threadID.xy] = float4(
    0.5f + u.x * uScale,
    0.5f + u.y * uScale,
    1.0f - length(u) * uScale,
    1.0f
    );
        
    
    */
    // Variante 2 - Geschwindigkeit
    float speed = length(u);
    float s = saturate(speed * 10.0f);

    g_OutputTexture[threadID.xy] = float4(s, 0.0f, 1.0f - s, 1.0f);
    /*
    
    // Variante 3 - Dichte
    float d = saturate((cell.rho - 1.0f) * 10.0f + 0.5f);
    g_OutputTexture[threadID.xy] = float4(d, d, d, 1.0f);
    // Variante 4 - Dichte clamped
    float d = saturate((cell.rho - 1.0f) * 100.0f); // kein +0.5
    g_OutputTexture[threadID.xy] = float4(d, d, d, 1.0f);
    */


}