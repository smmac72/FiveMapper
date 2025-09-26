struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    float2 pos[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };

    VSOut o;
    o.pos = float4(pos[vid], 0.0, 1.0);

    o.uv = 0.5 * (o.pos.xy + float2(1.0, 1.0));
    return o;
}
