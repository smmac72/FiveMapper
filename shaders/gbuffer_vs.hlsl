// simple g-buffer vertex shader
// we use row-major to avoid transposes on cpu
#pragma pack_matrix(row_major)

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float4 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
};

struct VSOut
{
    float4 pos     : SV_Position;
    float3 nrm     : TEXCOORD0;
    float2 uv      : TEXCOORD1;
};

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
};

VSOut VSMain(VSIn v)
{
    VSOut o;

    float4 wp = mul(float4(v.pos, 1.0), gWorld);
    o.pos = mul(wp, gViewProj);

    // note: we keep normal in world space for now
    float3 n = mul(float4(v.normal, 0.0), gWorld).xyz;
    o.nrm = normalize(n);

    o.uv = v.uv;
    return o;
}
