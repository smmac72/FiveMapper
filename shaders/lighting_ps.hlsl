// lighting pixel shader: PBR (Cook–Torrance + GGX), directional light, no IBL, no shadows
// reads gbuffer g0..g3 + depth, reconstructs world position via invViewProj
// outputs sRGB to UNORM backbuffer

Texture2D G0      : register(t0); // albedo (0..1, will be interpreted as sRGB below)
Texture2D G1      : register(t1); // normal.xyz (0..1) + roughness
Texture2D G2      : register(t2); // metallic (r), ao (g), emissive (b)
Texture2D G3      : register(t3); // spare
Texture2D GDepth  : register(t4); // R32_FLOAT depth
SamplerState S0   : register(s0);

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

cbuffer CameraCB : register(b0)
{
    row_major float4x4 gInvViewProj; // row_major to match CPU layout
    float3 gCamPosWS; float _padCam;
};

cbuffer SunCB : register(b1)
{
    float3 gSunDirWS;   float gSunIntensity; // light travels along -dir
    float3 gSunColor;   float _padSun;
}

// debug views:
// 0=final, 1=albedo, 2=normal, 3=ndotl, 4=depth linear-ish viz, 5=roughness, 6=metallic, 7=ao
#ifndef DEBUG_VIEW
#define DEBUG_VIEW 0
#endif

// sRGB<->linear helpers (piecewise, safe)
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

// reconstruct world position from (uv, depth) with inverse view-projection
float3 reconstruct_world_pos(float2 uv, float depth)
{
    float2 xy_ndc = uv * 2.0 - 1.0;
    float  z_ndc  = depth;
    float4 p_clip = float4(xy_ndc, z_ndc, 1.0);
    float4 p_world = mul(p_clip, gInvViewProj);
    return p_world.xyz / max(p_world.w, 1e-6);
}

// fresnel (schlick)
float3 fresnel_schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ggx / trowbridge-reitz NDF
float D_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-7);
}

// smith ggx geometry (separable), schlick-ggx for G1
float G_Schlick_GGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k + 1e-7);
}
float G_Smith(float NdotV, float NdotL, float k)
{
    return G_Schlick_GGX(NdotV, k) * G_Schlick_GGX(NdotL, k);
}

float4 PSMain(PSIn i) : SV_Target
{
    // fetch gbuffer
    float3 a_srgb = G0.Sample(S0, i.uv).rgb;
    float4 n_r    = G1.Sample(S0, i.uv);
    float3 m_a_e  = G2.Sample(S0, i.uv).rgb;
    float  depth  = GDepth.Sample(S0, i.uv).r;

    static const float3 kBg = float3(0.05, 0.05, 0.06);
    if (depth >= 0.9995)
    {
        return float4(kBg, 1.0);
    }

    // unpack
    float3 N = normalize(n_r.xyz * 2.0 - 1.0);
    float  roughness = saturate(n_r.w);
    float  metallic  = saturate(m_a_e.r);
    float  ao        = saturate(m_a_e.g);
    // emissive (m_a_e.b) пока не используем

    // albedo to linear
    float3 albedo = srgb_to_linear(a_srgb);

#if DEBUG_VIEW == 1
    return float4(a_srgb, 1);
#elif DEBUG_VIEW == 2
    return float4(0.5 * (N + 1.0), 1);
#elif DEBUG_VIEW == 3
    float3 Ld_dbg = normalize(-gSunDirWS);
    float ndotl_dbg = saturate(dot(N, Ld_dbg));
    return float4(ndotl_dbg, ndotl_dbg * 0.5, 0.0, 1);
#elif DEBUG_VIEW == 4
    return float4(depth.xxx, 1);
#elif DEBUG_VIEW == 5
    return float4(roughness.xxx, 1);
#elif DEBUG_VIEW == 6
    return float4(metallic.xxx, 1);
#elif DEBUG_VIEW == 7
    return float4(ao.xxx, 1);
#endif

    // reconstruct world position and directions
    float3 P = reconstruct_world_pos(i.uv, depth);
    float3 V = normalize(gCamPosWS - P);
    float3 L = normalize(-gSunDirWS);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // base reflectivity F0: dielectric 0.04, mix with albedo for metallic
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // fresnel
    float3  F = fresnel_schlick(VdotH, F0);

    // roughness remap for geometry term (schlick-ggx uses k = (a+1)^2 / 8)
    float  a = max(roughness * roughness, 1e-4);
    float  k = ((a + 1.0) * (a + 1.0)) / 8.0;

    // geometry
    float  G = G_Smith(NdotV, NdotL, k);

    // normal distribution
    float  D = D_GGX(NdotH, a);

    // specular term
    float3 spec = (F * G * D) / max(4.0 * NdotV * NdotL, 1e-6);

    // diffuse term (energy-conserving lambert), metals have less diffuse
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diff = kd * albedo / 3.14159265;

    float3 direct = (diff + spec) * gSunColor * (gSunIntensity * NdotL);

    // cheap ambient using ao (no ibl yet)
    float3 ambient = albedo * 0.03 * ao;

    float3 color_linear = direct + ambient;

    // to sRGB for UNORM backbuffer
    float3 out_srgb = linear_to_srgb(color_linear);
    return float4(out_srgb, 1.0);
}
