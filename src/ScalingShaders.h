// =============================================================================
// ScalingShaders.h — Code HLSL des shaders d'agrandissement
// =============================================================================
//
// Un SHADER est un petit programme exécuté par le GPU, en parallèle, pour
// chaque sommet (vertex shader) ou chaque pixel (pixel shader) d'un dessin.
// Il s'écrit en HLSL (High-Level Shading Language), un langage proche du C,
// et est compilé au lancement par D3DCompile (ShaderScaler.cpp).
// Voir docs/API_WINDOWS.md §4.7.
//
// Le texte est stocké dans une chaîne C++ « brute » R"HLSL(...)HLSL" : un seul
// fichier source, rien à copier à côté de l'exécutable. Les commentaires HLSL
// sont volontairement SANS ACCENTS : le compilateur HLSL attend de l'ASCII.
//
// Trois passes, chacune dessinant un triangle qui couvre toute la cible :
//   A. PSConvert : NV12 (Y + UV) -> RGB, à la taille de l'image source
//   B. PSSharpen : renforcement de netteté adaptatif (optionnel), taille source
//   C. PSScaleBicubic / PSScaleLanczos : agrandissement vers l'écran, dans le
//      rectangle de destination (les bandes noires sont effacées avant)

#pragma once

namespace hdmi {

// [C++] « inline constexpr » : constante définie dans un en-tête sans créer
// de doublon à l'édition de liens (C++17).
inline constexpr char kScalingShadersHlsl[] = R"HLSL(
// ---------------------------------------------------------------------------
// Parametres communs, mis a jour a chaque image (constant buffer, registre b0)
// ---------------------------------------------------------------------------
cbuffer Params : register(b0)
{
    float2 srcSize;     // taille de l'image source, en pixels (ex. 1920 x 1080)
    float2 dstOrigin;   // coin haut-gauche du rectangle de destination (pixels ecran)
    float2 dstSize;     // taille du rectangle de destination (ex. 2880 x 1620)
    float  sharpness;   // 0 = nettete douce ... 1 = forte (passe B)
    float  antiRinging; // 0 = aucun ecretage des halos ... 1 = ecretage complet
    float  yOffset;     // conversion de plage : Y' = (Y - yOffset) * yScale
    float  yScale;
    float  cScale;      // et C' = (C - 128/255) * cScale
    float  unused;      // remplissage : taille multiple de 16 octets
};

// Textures lues. Registres distincts pour que chaque passe ait les siens.
Texture2D<float>  planeY  : register(t0);   // NV12 : plan de luminance (R8)
Texture2D<float2> planeUV : register(t1);   // NV12 : plan de chrominance (R8G8, demi-resolution)
Texture2D<float4> image   : register(t2);   // image RGB intermediaire (passes B et C)

SamplerState linearClamp : register(s0);    // filtrage bilineaire, bords repetes

// ---------------------------------------------------------------------------
// Vertex shader : un triangle qui couvre toute la cible, sans tampon de sommets.
// Les 3 sommets (id 0, 1, 2) sont calcules a partir de leur numero.
// ---------------------------------------------------------------------------
struct VSOut
{
    float4 pos : SV_Position;   // dans le pixel shader : coordonnees du pixel + 0.5
};

VSOut VSFullscreen(uint id : SV_VertexID)
{
    float2 t = float2((id << 1) & 2, id & 2);          // (0,0), (2,0), (0,2)
    VSOut o;
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// ---------------------------------------------------------------------------
// Passe A : NV12 -> RGB (norme BT.709, celle de la HD)
// ---------------------------------------------------------------------------
float4 PSConvert(VSOut input) : SV_Target
{
    int2 p = int2(input.pos.xy);
    float y = planeY.Load(int3(p, 0));
    // Chrominance a demi-resolution : interpolation bilineaire au meme endroit.
    float2 c = planeUV.SampleLevel(linearClamp, input.pos.xy / srcSize, 0);

    y = (y - yOffset) * yScale;
    c = (c - 128.0 / 255.0) * cScale;

    float3 rgb;
    rgb.r = y + 1.5748 * c.y;
    rgb.g = y - 0.1873 * c.x - 0.4681 * c.y;
    rgb.b = y + 1.8556 * c.x;
    return float4(saturate(rgb), 1.0);
}

// Lecture d'un pixel de l'image RGB, avec repetition des bords.
float3 fetch(int2 p)
{
    int2 last = int2(srcSize) - int2(1, 1);
    return image.Load(int3(clamp(p, int2(0, 0), last), 0)).rgb;
}

// ---------------------------------------------------------------------------
// Passe B : renforcement de nettete adaptatif
// Idee reprise de CAS (Contrast Adaptive Sharpening, AMD FidelityFX) : on
// renforce moins la ou le contraste local est deja fort, ce qui evite les halos.
// ---------------------------------------------------------------------------
float4 PSSharpen(VSOut input) : SV_Target
{
    int2 p = int2(input.pos.xy);
    float3 c     = fetch(p);
    float3 north = fetch(p + int2(0, -1));
    float3 south = fetch(p + int2(0, 1));
    float3 west  = fetch(p + int2(-1, 0));
    float3 east  = fetch(p + int2(1, 0));

    float3 mn = min(c, min(min(north, south), min(west, east)));
    float3 mx = max(c, max(max(north, south), max(west, east)));
    // Marge disponible avant de saturer, rapportee au maximum local.
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-5)));
    float peak = -1.0 / lerp(8.0, 5.0, sharpness);
    float3 w = amp * peak;   // poids (negatif) des 4 voisins
    float3 result = (c + (north + south + west + east) * w) / (1.0 + 4.0 * w);
    return float4(saturate(result), 1.0);
}

// ---------------------------------------------------------------------------
// Passe C : agrandissement par filtre 4 x 4
// ---------------------------------------------------------------------------
// Poids de Catmull-Rom (bicubique " net ") pour les 4 pixels autour de la
// position, selon la partie fractionnaire t de la position.
float4 weightsCatmullRom(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return float4(-0.5 * t3 + t2 - 0.5 * t,
                   1.5 * t3 - 2.5 * t2 + 1.0,
                  -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                   0.5 * t3 - 0.5 * t2);
}

// Noyau de Lanczos a 2 lobes : sinc(x) * sinc(x / 2), nul au-dela de |x| = 2.
float lanczos2(float x)
{
    x = abs(x);
    if (x < 1e-5) return 1.0;
    if (x >= 2.0) return 0.0;
    float px = 3.14159265 * x;
    return 2.0 * sin(px) * sin(px * 0.5) / (px * px);
}

float4 weightsLanczos2(float t)
{
    float4 w = float4(lanczos2(1.0 + t), lanczos2(t), lanczos2(1.0 - t), lanczos2(2.0 - t));
    return w / dot(w, float4(1.0, 1.0, 1.0, 1.0));   // somme des poids = 1
}

float3 filter4x4(float2 srcPos, bool useLanczos)
{
    // Centre des pixels en x + 0.5 : on se ramene a une grille entiere.
    float2 p = srcPos - 0.5;
    float2 cell = floor(p);
    float2 f = p - cell;
    float4 wx = useLanczos ? weightsLanczos2(f.x) : weightsCatmullRom(f.x);
    float4 wy = useLanczos ? weightsLanczos2(f.y) : weightsCatmullRom(f.y);
    int2 origin = int2(cell) - int2(1, 1);

    float3 sum = float3(0.0, 0.0, 0.0);
    [unroll] for (int j = 0; j < 4; ++j)
    {
        float3 row = fetch(origin + int2(0, j)) * wx.x
                   + fetch(origin + int2(1, j)) * wx.y
                   + fetch(origin + int2(2, j)) * wx.z
                   + fetch(origin + int2(3, j)) * wx.w;
        sum += row * wy[j];
    }

    // Anti-halo : ces filtres " depassent " un peu pres des contours nets (c'est
    // ce qui les rend nets), ce qui cree un lisere clair autour du texte fonce.
    // On rapproche le resultat de l'intervalle des 4 pixels les plus proches.
    float3 a = fetch(origin + int2(1, 1));
    float3 b = fetch(origin + int2(2, 1));
    float3 c = fetch(origin + int2(1, 2));
    float3 d = fetch(origin + int2(2, 2));
    float3 lo = min(min(a, b), min(c, d));
    float3 hi = max(max(a, b), max(c, d));
    return saturate(lerp(sum, clamp(sum, lo, hi), antiRinging));
}

// Position dans l'image source correspondant au pixel ecran courant.
float2 sourcePosition(float2 screenPos)
{
    return (screenPos - dstOrigin) / dstSize * srcSize;
}

float4 PSScaleBicubic(VSOut input) : SV_Target
{
    return float4(filter4x4(sourcePosition(input.pos.xy), false), 1.0);
}

float4 PSScaleLanczos(VSOut input) : SV_Target
{
    return float4(filter4x4(sourcePosition(input.pos.xy), true), 1.0);
}
)HLSL";

} // namespace hdmi
