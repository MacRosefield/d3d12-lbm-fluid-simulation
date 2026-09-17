
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

// === D2Q9 Richtungsvektoren ===
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

// === Gegenrichtungen für Bounce-Back ===
static const int oppositeOf[9] = { 0, 3, 4, 1, 2, 7, 8, 5, 6 };

RWStructuredBuffer<LBCell> b_gridRead : register(u0); // aktueller Zustand
RWStructuredBuffer<LBCell> b_gridWrite : register(u1); // Ziel nach Streaming
RWTexture2D<float4> g_OutputTexture : register(u2);
RWStructuredBuffer<uint> b_cellTypes : register(u3); // Buffer für celltypen für waende obstacles etc


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
    uint dstIndex = y * gridWidth + x;

    if (x >= gridWidth || y >= gridHeight)
        return;

    uint type = b_cellTypes[dstIndex];
    LBCell result = b_gridRead[dstIndex];
    result.rho = b_gridRead[dstIndex].rho;
    result.u = b_gridRead[dstIndex].u;

    // === INFLOW ===
    if (type == 1)
    {
        /*
        float yNorm = float(y) / float(gridHeight - 1);
        float uMax = 0.05f;
        float ux = uMax * (1.0 - pow(2.0 * yNorm - 1.0, 2.0));
        float2 u_in = float2(ux, 0.0f);
        float rho_in = 1.0f;

        for (int i = 0; i < 9; ++i)
        {
            result.f[i] = computeEquilibrium(i, rho_in, u_in);
        }

        g_OutputTexture[threadID.xy] = float4(0.0, 1.0, 0.0, 1.0); // GRÜN
        b_gridWrite[dstIndex] = result;
*/
        return;
    }

    // === OUTFLOW ===
    if (type == 2)
    {
        LBCell result;
        result.rho = 1.0f;
        result.u = float2(outflow_u, 0.0f);
        
        float2 targetU = float2(outflow_u, 0.0f);
        float rho = 1.0f; // Ziel-Dichte

        for (int i = 0; i < 9; ++i)
        {
            float feq = computeEquilibrium(i, rho, targetU);

        // Dämpfe stromaufwärts-Verteilung gegen dieses Gleichgewicht
            int2 offset = int2(c[i].x, c[i].y);
            int srcX = int(x - offset.x);
            int srcY = int(y - offset.y);

            float fi = feq; // fallback: Gleichgewicht

            if (srcX >= 0 && srcX < gridWidth && srcY >= 0 && srcY < gridHeight)
            {
                uint srcIndex = srcY * gridWidth + srcX;
                float neighborF = b_gridRead[srcIndex].f[i];

        // Jetzt: Mischung zwischen vorhandener Verteilung und Gleichgewicht
                fi = lerp(feq, neighborF, 0.95f); //Zielgeschwindigkeit
            }
            
             // Wenn Richtung in die Zelle hineingeht dämpfen gegen feq
            if (i == 3 || i == 6 || i == 7) // Richtung -x (links), diag links
            {
                float feq = computeEquilibrium(i, rho, targetU);
                fi = lerp(fi, feq, 0.5f); // sanft annähern, nicht hart setzen
            }

            result.f[i] = fi;
        }

        b_gridWrite[dstIndex] = result;
        return;
    }

    // === OBSTACLE ===
    if (type == 3)
    {
        for (int i = 0; i < 9; ++i)
        {
            int opp = oppositeOf[i];
            result.f[i] = b_gridRead[dstIndex].f[opp]; // Bounce-Back
        }

        g_OutputTexture[threadID.xy] = float4(0.0, 0.0, 0.0, 1.0); // SCHWARZ
        b_gridWrite[dstIndex] =
result;
        return;
    }

    // === WALL ===
    if (type == 4)
    {
        for (int i = 0; i < 9; ++i)
        {
            int opp = oppositeOf[i];
            result.f[i] = b_gridRead[dstIndex].f[opp];
        }

        g_OutputTexture[threadID.xy] = float4(1.0, 0.0, 0.0, 1.0); // ROT
        b_gridWrite[dstIndex] =
result;
        return;
    }
    
    
    // === FLUID (Default) ===
    
    if (type == 0)
    {
        for (
            int i = 0; i < 9; ++i)
        {
            int2 offset = c[i];
            int srcX = int(x) - offset.x;
            int srcY = int(y) - offset.y;

            if (srcX >= 0 && srcX < gridWidth && srcY >= 0 && srcY < gridHeight)
            {
                int srcIndex = srcY * gridWidth + srcX;
                result.f[i] = b_gridRead[srcIndex].
            f[i];
            }
            else
            {
                result.f[i] = b_gridRead[dstIndex].
            f[i]; // Erhalte Wert
            }
        }
    }

    //g_OutputTexture[threadID.xy] = float4(1.0, 1.0, 1.0, 1.0); // WEISS
    b_gridWrite[dstIndex] = result;
    

}

