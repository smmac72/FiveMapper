cbuffer LightCB : register(b0)
{
    row_major float4x4 gLightViewProj;
};

cbuffer ObjectCB : register(b1)
{
    row_major float4x4 gWorld;
};

struct VSIn
{
    float3 pos  : POSITION;
    float3 nrm  : NORMAL;
    float4 tan  : TANGENT;
    float2 uv   : TEXCOORD0;
};

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 pw = mul(float4(v.pos, 1.0), gWorld);
    o.pos = mul(pw, gLightViewProj);
    return o;
}
