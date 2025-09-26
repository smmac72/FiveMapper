// fullscreen triangle vs
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    float2 pos = p[vid];

    VSOut o;
    o.pos = float4(pos, 0, 1);
    o.uv  = 0.5 * (pos + 1.0);
    return o;
}
