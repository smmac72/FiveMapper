cbuffer CameraCB   : register(b0) { float4x4 ViewProj; float3 camPos; };
cbuffer SunCB      : register(b1) { float3 sunDir; float3 sunColor; };

Texture2D G0 : register(t0); // Albedo
Texture2D G1 : register(t1); // Normal.xy + Rough
Texture2D G2 : register(t2); // Spec + Emiss.r
Texture2D G3 : register(t3); // Extra (height or so)
SamplerState samp: register(s0);

struct PSIn { float4 posSV : SV_POSITION; float2 uv : TEXCOORD0; };

float DistributionGGX(float NdotH, float rough)
{
    float a = rough*rough;
    float a2= a*a;
    float denom = (NdotH*NdotH)*(a2-1) + 1;
    return a2 / (3.14159265 * denom*denom);
}
float GeometrySchlick(float NdotV, float rough)
{
    float r = (rough+1);
    float k = (r*r)/8;
    return NdotV / (NdotV*(1-k)+k);
}
float GeometrySmith(float NdotV, float NdotL, float rough)
{
    float ggx1 = GeometrySchlick(NdotV, rough);
    float ggx2 = GeometrySchlick(NdotL, rough);
    return ggx1 * ggx2;
}
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1-F0)*pow(1-cosTheta,5);
}

float4 PSMain(PSIn IN) : SV_Target0
{
    float4  albedo     = G0.Sample(samp, IN.uv);
    float3  normal_xy  = G1.Sample(samp, IN.uv).xy * 2-1;
    float   roughness  = G1.Sample(samp, IN.uv).z;
    float3  specMetal  = G2.Sample(samp, IN.uv).rg;
    float3  emissive   = G2.Sample(samp, IN.uv).baa; // b->r, a->g etc as you packed

    float3 N = normalize(float3(normal_xy, sqrt(saturate(1 - dot(normal_xy, normal_xy)))));
    float3 V = normalize(camPos - ( ViewProj[3].xyz )); // approx world pos
    float3 L = normalize(-sunDir);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N,L), 0);
    float NdotV = max(dot(N,V), 0);
    float NdotH = max(dot(N,H), 0);
    float VdotH = max(dot(V,H), 0);

    float3 F0 = lerp(float3(0.04,0.04,0.04), albedo.rgb, specMetal.g);
    float  D  = DistributionGGX(NdotH, roughness);
    float  G  = GeometrySmith(NdotV, NdotL, roughness);
    float3 F  = FresnelSchlick(VdotH, F0);

    float3 numerator   = D * G * F;
    float  denom       = 4 * NdotV * NdotL + 0.001;
    float3 specular    = numerator / denom;

    float3 kD = (1 - F) * (1 - specMetal.g);
    float3 diffuse = kD * albedo.rgb / 3.14159265;

    float3 color = (diffuse + specular) * sunColor * NdotL + emissive;

    // Reinhard tone-mapping
    color = color / (color + 1);

    return float4(color, albedo.a);
}
