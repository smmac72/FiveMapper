// gbuffer vertex shader
// transforms vertex to clip, passes normal/tangent/uv in world space

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProj; // row_major handled on CPU side; we treat as standard here
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
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
    float4 pos  : SV_Position;
    float3 nrmW : TEXCOORD0;
    float4 tanW : TEXCOORD1;
    float2 uv   : TEXCOORD2;
};

VSOut VSMain(VSIn v)
{
    VSOut o;

    float4 pw = mul(float4(v.pos, 1.0), gWorld);
    float3 nw = normalize(mul(float4(v.nrm, 0.0), gWorld).xyz);
    float4 tw = float4(normalize(mul(float4(v.tan.xyz, 0.0), gWorld).xyz), v.tan.w);

    o.pos = mul(pw, gViewProj);
    o.nrmW = nw;
    o.tanW = tw;
    o.uv = v.uv;

    return o;
}
