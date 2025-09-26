// minimal deferred lighting: directional lambert (safe sRGB/linear + debug views)
// now reads depth (t4)

Texture2D G0 : register(t0); // albedo
Texture2D G1 : register(t1); // normal.xyz (0..1) + roughness
Texture2D G2 : register(t2); // metallic/ao/emissive
Texture2D G3 : register(t3); // spare
Texture2D GDepth : register(t4); // R32_FLOAT depth (nonlinear)
SamplerState S0 : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// in lighting pass we keep a separate camera cb
cbuffer CameraCB : register(b0)
{
    float4x4 gInvViewProj; // for future position reconstruction
};

cbuffer SunCB : register(b1)
{
    float3 gSunDirWS;   float gSunIntensity;
    float3 gSunColor;   float _pad0;
}

// 0=lighting, 1=albedo, 2=normal, 3=ndotl, 4=depth
#ifndef DEBUG_VIEW
#define DEBUG_VIEW 0
#endif

float3 srgb_to_linear(float3 c)
{
    c = saturate(c);
    float3 lo = c / 12.92;
    float3 hi = pow(max((c + 0.055) / 1.055, 0.0), 2.4);
    return lerp(lo, hi, step(0.04045, c));
}

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
    float3 a_srgb = G0.Sample(S0, i.uv).rgb;
    float4 npack  = G1.Sample(S0, i.uv);
    float  z      = GDepth.Sample(S0, i.uv).r; // nonlinear depth 0..1

#if DEBUG_VIEW == 1
    return float4(a_srgb, 1);
#elif DEBUG_VIEW == 2
    float3 Ndbg = normalize(npack.xyz * 2.0 - 1.0);
    return float4(0.5 * (Ndbg + 1.0), 1);
#elif DEBUG_VIEW == 3
    float3 N = normalize(npack.xyz * 2.0 - 1.0);
    float3 Ld = normalize(-gSunDirWS);
    float ndotl = saturate(dot(N, Ld));
    return float4(ndotl, ndotl * 0.5, 0.0, 1);
#elif DEBUG_VIEW == 4
    // visualize depth (bright = far)
    return float4(z.xxx, 1);
#else
    // normal shaded lambert
    float3 albedo = srgb_to_linear(a_srgb);
    float3 N = normalize(npack.xyz * 2.0 - 1.0);
    float3 Ld = normalize(-gSunDirWS);
    float ndotl = saturate(dot(N, Ld));

    float3 lit_linear = albedo * gSunColor * (gSunIntensity * ndotl);
    float3 out_srgb = linear_to_srgb(lit_linear);
    return float4(out_srgb, 1.0);
#endif
}
