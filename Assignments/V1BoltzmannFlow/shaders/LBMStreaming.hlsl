


struct LBCell
{
    float rho; // Dichte
    float2 u; // Geschwindigkeit (x, y)
    float f[9]; // Verteilungsfunktionen für D2Q9
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


RWStructuredBuffer<LBCell> b_gridRead : register(u0); // aktueller Zustand
RWStructuredBuffer<LBCell> b_gridWrite : register(u1); // Ziel nach Streaming
RWTexture2D<float4> g_OutputTexture : register(u2);
RWStructuredBuffer<uint> b_cellTypes : register(u3); // Buffer für celltypen für waende obstacles etc

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    

// === D2Q9 Richtungsvektoren ===
    static const int2 c[9] =
    {
        int2(0, 0),
    int2(1, 0), int2(-1, 0),
    int2(0, 1), int2(0, -1),
    int2(1, 1), int2(-1, -1),
    int2(-1, 1), int2(1, -1)
    };
    
// === Gegenrichtungen für Bounce-Back ===
    static const int oppositeOf[9] = { 0, 3, 4, 1, 2, 7, 8, 5, 6 };

    
    uint gridWidth = 640;
    uint gridHeight = 480;
    
    uint x = threadID.x;
    uint y = threadID.y;

    if (x >= gridWidth || y >= gridHeight)
        return;

    uint dstIndex = y * gridWidth + x;
    uint type = b_cellTypes[dstIndex];
    LBCell result = b_gridRead[dstIndex];;
    
    // Optional: rho und u übernehmen (nicht zwingend nötig, da sie im Collision neu berechnet werden)
    result.rho = b_gridRead[dstIndex].rho;
    result.u = b_gridRead[dstIndex].u;
    
    // === Standard-Streaming für Fluid / Inflow / Outflow ===
    // Für jede Richtung i: nimm f[i] aus Nachbarzelle entlang -c[i]
    for (int i = 0; i < 9; ++i)
    {
        int2 offset = c[i];
        int srcX = int(x) - offset.x;
        int srcY = int(y) - offset.y;

        
        
        if (srcX >= 0 && srcX < gridWidth && srcY >= 0 && srcY < gridHeight)
        {
            uint srcIndex = srcY * gridWidth + srcX;
            result.f[i] = b_gridRead[srcIndex].f[i];
           
        }
        else
        {
            if (type == 4 || type == 3)
            {
                // einfache bounce-back boundary: reflektiere f[i]   
                int opp = oppositeOf[i]; // D2Q9-Tabelle nutzen
                result.f[i] = b_gridRead[dstIndex].f[opp];
                //result.f[i] *= 0.99f;
                g_OutputTexture[threadID.xy] = float4(1.0f, 0.0f, 0.0f, 1.0f); // ROT
            }
            else if (type == 2)
            {
                
                  // outflow mit f[i] = w[i]            
                int opp = oppositeOf[i]; // D2Q9-Tabelle nutzen
                result.f[i] = b_gridRead[dstIndex].f[opp];
                result.f[i] = w[i];
                g_OutputTexture[threadID.xy] = float4(0.0f, 0.0f, 1.0f, 1.0f); // BLAU
                
            }
            else
            {
                
            // einfache outflow mit f[i] = w[i]            
                int opp = oppositeOf[i]; // D2Q9-Tabelle nutzen
                result.f[i] = b_gridRead[dstIndex].f[opp];
                result.f[i] = w[i];
                g_OutputTexture[threadID.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f); // WEISS
            }
            
        }
    }
           // === Speichern ===
    b_gridWrite[dstIndex] = result;
    
    if (threadID.x == gridWidth / 2)
    {
        g_OutputTexture[threadID.xy] = float4(0.2f, 0.2f, 1.0f, 1.0f); // Hellblauer Streifen
    }
}
