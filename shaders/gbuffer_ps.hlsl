Texture2D GBufferTex[7] : register(t0);  
SamplerState sampLinear : register(s0);

struct PSInput
{
    float4 posSV : SV_POSITION;
    float3 normal: NORMAL;
    float3 tangent: TANGENT;
    float2 uv    : TEXCOORD0;
};

struct PSOutput
{
    float4 RTV0 : SV_Target0;   // Albedo + Occ
    float4 RTV1 : SV_Target1;   // Normal.xy + Roughness
    float4 RTV2 : SV_Target2;   // Specular + Emissive
    float4 RTV3 : SV_Target3;   // Extra
};

PSOutput PSMain(PSInput IN)
{
    PSOutput OUT;

    float4 albedo    = GBufferTex[0].Sample(sampLinear, IN.uv);
    float4 normalMap = GBufferTex[1].Sample(sampLinear, IN.uv);
    float4 orm       = GBufferTex[2].Sample(sampLinear, IN.uv);
    float4 emissive  = GBufferTex[3].Sample(sampLinear, IN.uv);
    float4 height    = GBufferTex[4].Sample(sampLinear, IN.uv);
    float4 detailAlb = GBufferTex[5].Sample(sampLinear, IN.uv);
    float4 detailNrm = GBufferTex[6].Sample(sampLinear, IN.uv);

    OUT.RTV0 = albedo;
    OUT.RTV1 = float4(normalMap.xy, orm.g, 1.0);
    OUT.RTV2 = float4(orm.r, orm.b, emissive.r, 1.0); 
    OUT.RTV3 = float4(height.r, detailAlb.r, detailNrm.r, 1.0);

    return OUT;
}
