// fullscreen-triangle, positions in clip-space
static const float2 vsPos[3] = {
    float2(-1, -1),
    float2(-1,  3),
    float2( 3, -1),
};

struct VSOut {
    float4 posSV : SV_POSITION;
    float2 uv    : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    o.posSV = float4(vsPos[vid], 0, 1);
    // map clip-space to UV [0,1]
    o.uv    = (vsPos[vid] * 0.5f) + 0.5f;
    return o;
}
