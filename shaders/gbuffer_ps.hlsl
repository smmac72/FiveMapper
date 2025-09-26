// gbuffer pixel shader (one PS writes to 4 MRTs)

struct PSIn
{
    float4 pos  : SV_Position;
    float3 nrmW : TEXCOORD0;
    float4 tanW : TEXCOORD1;
    float2 uv   : TEXCOORD2;
};

struct GBufferOut
{
    float4 G0 : SV_Target0; // albedo
    float4 G1 : SV_Target1; // normal.xyz (0..1) + roughness
    float4 G2 : SV_Target2; // metallic, ao, emissive, _
    float4 G3 : SV_Target3; // spare
};

// тестовый материал
static const float3 kAlbedo    = float3(0.8, 0.45, 0.3);
static const float  kRoughness = 0.6;
static const float  kMetallic  = 0.1;
static const float  kAO        = 1.0;

GBufferOut PSMain(PSIn i)
{
    GBufferOut o;

    float3 N = normalize(i.nrmW);
    float3 Nenc = 0.5 * (N + 1.0);

    o.G0 = float4(kAlbedo, 1.0);
    o.G1 = float4(Nenc, saturate(kRoughness));
    o.G2 = float4(saturate(kMetallic), saturate(kAO), 0.0, 1.0);
    o.G3 = float4(0,0,0,1);

    return o;
}
