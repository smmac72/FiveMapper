// simple g-buffer pixel shader
// we do not sample any textures yet
// we just pack reasonable defaults to test mrt

struct PSIn
{
    float4 pos : SV_Position;
    float3 nrm : TEXCOORD0;
    float2 uv  : TEXCOORD1;
};

struct PSOut
{
    float4 g0 : SV_Target0; // albedo + alpha
    float4 g1 : SV_Target1; // normal.xyz + roughness
    float4 g2 : SV_Target2; // metallic + occlusion + emissive
    float4 g3 : SV_Target3; // reserved / extra
};

PSOut PSMain(PSIn i)
{
    PSOut o;

    // write neutral albedo
    o.g0 = float4(0.8, 0.75, 0.7, 1.0);

    // encode normal in 0..1 and roughness in alpha
    float3 n = normalize(i.nrm) * 0.5 + 0.5;
    o.g1 = float4(n, 0.5); // roughness = 0.5

    // metallic, occlusion, emissive
    o.g2 = float4(0.0, 1.0, 0.0, 0.0); // met=0, ao=1, emissive=0

    // leave as zero
    o.g3 = float4(0,0,0,0);

    return o;
}
