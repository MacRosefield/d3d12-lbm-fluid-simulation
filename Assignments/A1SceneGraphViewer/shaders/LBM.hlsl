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


// === Richtungsvektoren (D2Q9) ===
static const float2 c[9] =
{
    float2(0, 0),
    float2(1, 0), float2(-1, 0),
    float2(0, 1), float2(0, -1),
    float2(1, 1), float2(-1, -1),
    float2(-1, 1), float2(1, -1)
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


// Muss exakt mit C++ enum übereinstimmen
static const uint Cell_Fluid = 0;
static const uint Cell_Inflow = 1;
static const uint Cell_Outflow = 2;
static const uint Cell_Obstacle = 3;
static const uint Cell_Wall = 4;

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
    
    /*
// === Wände oder Hindernisse: keine Kollision ===
    if (cellType == 4 || cellType == 3)
    {
        // keine Kollision – Streaming übernimmt bounce-back
        return;
    }
    */
    

        // === Inflow-Zellen: Geschwindigkeit erzwingen ===
    if (cellType == 1 )
    {
        float2 u_in = float2(0.1f, 0.0f); // gewünschte Inflow-Geschwindigkeit
        float rho_in = 1.0f;
        
        float u2 = dot(u_in, u_in);

        cell.rho = rho_in;
        cell.u = u_in;

        for (int i = 0; i < 9; i++)
        {
            float cu = dot(c[i], u_in);
            cell.f[i] = w[i] * rho_in * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * u2);
        }

        b_gridWrite[index] = cell;

        g_OutputTexture[threadID.xy] = float4(1.0, 0.0, 1.0, 1.0); // LILA = Inflow
        return;
    }

  

 // === Standardzellen: Fluid oder Outflow ===

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
    float tau = 0.6f;

    for (int i = 0; i < 9; i++)
    {
        float cu = dot(c[i], u);
        feq[i] = w[i] * rho * (1.0 + 3.0 * cu + 4.5 * cu * cu - 1.5 * u2);
        cell.f[i] += (feq[i] - cell.f[i]) / tau;
    }
    

    // Speichern
    b_gridWrite[index] = cell;

    // === Visualisierung
    float uScale = 5.0f;
    g_OutputTexture[threadID.xy] = float4(
    0.5f + u.x * uScale,
    0.5f + u.y * uScale,
    1.0f - length(u) * uScale,
    1.0f
);

}