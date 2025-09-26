// minimal deferred lighting: directional lambert (safe sRGB/linear + debug views)

Texture2D G0 : register(t0); // albedo (stored as UNORM)
Texture2D G1 : register(t1); // normal.xyz in 0..1 + roughness
Texture2D G2 : register(t2); // metallic/ao/emissive (unused yet)
SamplerState S0 : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj; // reserved
};

cbuffer SunCB : register(b1)
{
    float3 gSunDirWS;   float gSunIntensity; // note: light travels along -gSunDirWS
    float3 gSunColor;   float _pad0;
}

// debug selector: 0=lighting, 1=albedo, 2=normal, 3=ndotl heat
#ifndef DEBUG_VIEW
#define DEBUG_VIEW 0
#endif

// IEC 61966-2-1 piecewise sRGB -> linear (vectorized)
float3 srgb_to_linear(float3 c)
{
    c = saturate(c);
    float3 lo = c / 12.92;
    float3 hi = pow(max((c + 0.055) / 1.055, 0.0), 2.4);
    return lerp(lo, hi, step(0.04045, c));
}

// linear -> sRGB
float3 linear_to_srgb(float3 c)
{
    c = max(c, 0.0);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    float3 outc = lerp(lo, hi, step(0.0031308, c));
    return saturate(outc);
}

float4 PSMain(PSIn i) : SV_Target
{
    // fetch gbuffer
    float3 a_srgb = G0.Sample(S0, i.uv).rgb;
    float4 npack  = G1.Sample(S0, i.uv);

    // albedo (convert to linear for math)
    float3 albedo = srgb_to_linear(a_srgb);

    // unpack and normalize normal
    float3 N = normalize(npack.xyz * 2.0 - 1.0);

#if DEBUG_VIEW == 1
    // show albedo (sRGB)
    return float4(a_srgb, 1);
#elif DEBUG_VIEW == 2
    // show normal remapped to 0..1
    return float4(0.5 * (N + 1.0), 1);
#elif DEBUG_VIEW == 3
    // show ndotl heatmap
    float3 Ld = normalize(-gSunDirWS);
    float ndotl = saturate(dot(N, Ld));
    return float4(ndotl, ndotl * 0.5, 0.0, 1);
#else
    // lambert
    float3 Ld = normalize(-gSunDirWS); // light direction (to surface)
    float ndotl = saturate(dot(N, Ld));

    float3 lit_linear = albedo * gSunColor * (gSunIntensity * ndotl);

    // back to sRGB for UNORM backbuffer
    float3 out_srgb = linear_to_srgb(lit_linear);
    return float4(out_srgb, 1.0);
#endif
}
