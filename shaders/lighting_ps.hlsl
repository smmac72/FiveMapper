// lighting pixel shader: pbr + single shadow map (manual pcf 3x3), directional light

Texture2D G0      : register(t0);
Texture2D G1      : register(t1);
Texture2D G2      : register(t2);
Texture2D G3      : register(t3);
Texture2D GDepth  : register(t4);
Texture2D<float> ShadowMap : register(t5);

// s0 is reserved for linear sampling (materials/post); g-buffer uses point
SamplerState            S0       : register(s0);
SamplerState            SPoint   : register(s1);
SamplerComparisonState  SShadow  : register(s2);

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

cbuffer CameraCB : register(b0)
{
    // correct world position reconstruction: ndc -> invProj -> /w -> invView
    row_major float4x4 gInvProj;
    row_major float4x4 gInvView;

    float3 gCamPosWS; float _padCam;
};

cbuffer SunCB : register(b1)
{
    float3 gSunDirWS;   float gSunIntensity;
    float3 gSunColor;   float _pad0;

    row_major float4x4 gLightViewProj;
    float2 gShadowTexelSize; // 1/width, 1/height
    float  gShadowBias;      // base bias ~0.001..0.003
    float  gShadowStrength;  // 0..1
};

// debug view bitmask (set via compile-time define if needed)
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

// ndc(x,y,z) -> invProj -> divide by w -> invView
float3 reconstruct_world_pos(float2 uv, float depth01)
{
    float2 xy_ndc = uv * 2.0 - 1.0;
    float  z_ndc  = depth01; // d3d is already 0..1
    float4 p_ndc  = float4(xy_ndc, z_ndc, 1.0);

    float4 p_view = mul(p_ndc, gInvProj);
    p_view /= max(p_view.w, 1e-6);

    float4 p_world = mul(float4(p_view.xyz, 1.0), gInvView);
    return p_world.xyz;
}

// pbr helpers
float3 fresnel_schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}
float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d + 1e-7);
}
float G_Schlick_GGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k + 1e-7);
}
float G_Smith(float NdotV, float NdotL, float k)
{
    return G_Schlick_GGX(NdotV, k) * G_Schlick_GGX(NdotL, k);
}

// project to light space; returns false when outside [0,1]
bool ProjectToShadow(float3 P, out float2 uv, out float z)
{
    float4 pL = mul(float4(P,1), gLightViewProj);
    float  w  = max(pL.w, 1e-6);
    uv = pL.xy / w * 0.5 + 0.5;
    z  = pL.z / w; // d3d 0..1
    if (any(uv < 0.0) || any(uv > 1.0))
        return false;
    return true;
}

// returns 0..1 light visibility with 3x3 pcf, texel-based and angle-based bias
float ShadowVisibility(float3 P, float3 N, float3 L)
{
    float2 uv; 
    float  z;
    if (!ProjectToShadow(P, uv, z))
        return 1.0;

    // receiver-side bias: base + texel + angle
    const float kTexelBias  = 2.0;    // 1..3, scales with resolution
    const float kAngleBias  = 0.002;  // small additive for grazing angles

    float ndotl = saturate(dot(N, L));
    float texel = max(gShadowTexelSize.x, gShadowTexelSize.y);

    float bias = gShadowBias
               + kTexelBias * texel
               + kAngleBias * (1.0 - ndotl);

    float cmp = z - bias;

    // 3x3 hardware pcf
    float2 t = gShadowTexelSize;
    float vis = 0.0;

    [unroll] for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll] for (int dx = -1; dx <= 1; ++dx)
        {
            float2 o = float2(dx, dy) * t;
            vis += ShadowMap.SampleCmpLevelZero(SShadow, uv + o, cmp);
        }
    }

    vis *= (1.0 / 9.0);
    return saturate(vis);
}


float4 PSMain(PSIn i) : SV_Target
{
    // g-buffer must be sampled with point clamp (no filtering)
    float  depth  = GDepth.Sample(SPoint, i.uv).r;

    static const float3 kBg = float3(0.05, 0.05, 0.06);
    if (depth >= 0.9995) return float4(kBg, 1);

    float3 a_srgb = G0.Sample(SPoint, i.uv).rgb;
    float4 n_r    = G1.Sample(SPoint, i.uv);
    float3 m_a_e  = G2.Sample(SPoint, i.uv).rgb;

    float3 N = normalize(n_r.xyz * 2.0 - 1.0);
    float  roughness = saturate(n_r.w);
    float  metallic  = saturate(m_a_e.r);
    float  ao        = saturate(m_a_e.g);

#if DEBUG_VIEW == 1
    return float4(a_srgb,1);
#elif DEBUG_VIEW == 2
    return float4(0.5*(N+1.0),1);
#elif DEBUG_VIEW == 4
    return float4(depth.xxx,1);
#elif DEBUG_VIEW == 5
    {
        float3 Pw = reconstruct_world_pos(i.uv, depth);
        float2 suv; float sz;
        bool inside = ProjectToShadow(Pw, suv, sz);
        float raw = inside ? ShadowMap.SampleLevel(SPoint, suv, 0).r : 1.0;
        return float4(raw.xxx, 1);
    }
#endif

    float3 albedo = srgb_to_linear(a_srgb);
    float3 P = reconstruct_world_pos(i.uv, depth);
    float3 V = normalize(gCamPosWS - P);
    float3 L = normalize(-gSunDirWS);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N,L));
    float NdotV = saturate(dot(N,V));
    float NdotH = saturate(dot(N,H));
    float VdotH = saturate(dot(V,H));

    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo, metallic);

    float3 F = fresnel_schlick(VdotH, F0);
    float  a = max(roughness*roughness, 1e-4);
    float  k = ((a+1.0)*(a+1.0))/8.0;
    float  G = G_Smith(NdotV, NdotL, k);
    float  D = D_GGX(NdotH, a);

    float3 spec = (F * G * D) / max(4.0 * NdotV * NdotL, 1e-6);
    float3 kd   = (1.0 - F) * (1.0 - metallic);
    float3 diff = kd * albedo / 3.14159265;

    float visibility = ShadowVisibility(P);
#if DEBUG_VIEW == 6
    return float4(visibility.xxx, 1);
#endif

    visibility = lerp(1.0 - gShadowStrength, 1.0, visibility);

    float3 direct = (diff + spec) * gSunColor * (gSunIntensity * NdotL) * visibility;
    float3 ambient = albedo * 0.03 * ao;

    float3 color_linear = direct + ambient;
    float3 out_srgb = linear_to_srgb(color_linear);
    return float4(out_srgb, 1);
}
