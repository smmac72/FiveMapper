cbuffer CameraCB : register(b0)
{
    matrix ViewProj;
};

struct VSInput
{
    float3 pos   : POSITION;
    float3 normal: NORMAL;
    float3 tangent: TANGENT;
    float2 uv    : TEXCOORD0;
};

struct PSInput
{
    float4 posSV : SV_POSITION;
    float3 normal: NORMAL;
    float3 tangent: TANGENT;
    float2 uv    : TEXCOORD0;
};

PSInput VSMain(VSInput IN)
{
    PSInput OUT;
    float4 worldPos = float4(IN.pos, 1.0);
    OUT.posSV       = mul(worldPos, ViewProj);
    OUT.normal      = IN.normal;
    OUT.tangent     = IN.tangent;
    OUT.uv          = IN.uv;
    return OUT;
}
