// gbuffer pixel shader (one ps writes to 4 mrts)
// now samples material textures: t0 albedo (srgb), t1 normal (linear), t2 orm (linear: r=ao, g=roughness, b=metallic), t3 emissive (linear)
// we still store albedo in srgb in g0 because lighting pass expects to convert it to linear

Texture2D AlbedoTex   : register(t0);
Texture2D NormalTex   : register(t1);
Texture2D ORMTex      : register(t2);
Texture2D EmissiveTex : register(t3);

SamplerState S0 : register(s0);

struct PSIn
{
    float4 pos  : SV_Position;
    float3 nrmW : TEXCOORD0;
    float4 tanW : TEXCOORD1;
    float2 uv   : TEXCOORD2;
};

struct GBufferOut
{
    float4 G0 : SV_Target0; // albedo in srgb
    float4 G1 : SV_Target1; // normal.xyz (0..1) + roughness
    float4 G2 : SV_Target2; // metallic, ao, emissive, _
    float4 G3 : SV_Target3; // spare
};

// default material values (used as fallback if texture data is zero/black)
static const float3 kAlbedo_linear = float3(0.8, 0.45, 0.3);
static const float  kRoughness     = 0.6;
static const float  kMetallic      = 0.1;
static const float  kAO            = 1.0;
static const float3 kEmissive      = float3(0.0, 0.0, 0.0);

// basic srgb helpers
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

// small helper: treat nearly zero vector as "no texture" and replace with fallback
bool is_near_black(float3 v)
{
    return all(v < 1e-4.xxx);
}

GBufferOut PSMain(PSIn i)
{
    GBufferOut o;

    // normalize per-vertex normal
    float3 N = normalize(i.nrmW);

    // sample albedo. srv for albedo is srgb, so sampling returns linear already.
    float3 albedo_lin = AlbedoTex.Sample(S0, i.uv).rgb;
    if (is_near_black(albedo_lin))
    {
        albedo_lin = kAlbedo_linear;
    }
    // store albedo as srgb in G0 (lighting pass expects srgb there)
    float3 albedo_srgb = linear_to_srgb(albedo_lin);
    o.G0 = float4(albedo_srgb, 1.0);

    // sample normal map (linear). for now we keep vertex normal (tbn and nm not wired yet).
    // we still sample to detect empty maps later if needed.
    float3 nm_lin = NormalTex.Sample(S0, i.uv).rgb; // 0..1
    // todo: add tbn and unpack normal map; for now ignore and keep N
    float3 Nenc = 0.5 * (normalize(N) + 1.0);

    // sample orm (linear) with expected packing: r=ao, g=roughness, b=metallic
    float3 orm = ORMTex.Sample(S0, i.uv).rgb;
    float ao        = orm.r;
    float roughness = orm.g;
    float metallic  = orm.b;

    // apply fallbacks if the map is effectively empty/black
    if (is_near_black(orm))
    {
        ao        = kAO;
        roughness = kRoughness;
        metallic  = kMetallic;
    }

    // sample emissive (linear), optional
    float3 emissive = EmissiveTex.Sample(S0, i.uv).rgb;
    if (is_near_black(emissive))
    {
        emissive = kEmissive;
    }

    // pack to gbuffer
    o.G1 = float4(Nenc, saturate(roughness));
    o.G2 = float4(saturate(metallic), saturate(ao), emissive.r, 1.0);
    o.G3 = float4(0, 0, 0, 1);

    return o;
}
