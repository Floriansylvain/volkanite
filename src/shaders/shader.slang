struct InstancedVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
    float3 inTangent;
    float3 instancePos;
    float3 instanceRotation;
};

struct CameraUniform {
    float4x4 view;
    float4x4 proj;
    float4 cameraPos;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    half3 fragColor;
    float2 fragTexCoord;
    half3 fragNormal;
    half3 fragTangent;
    
    float3 viewDiff; 
};

float4x4 eulerRotationMatrix(float3 angles) {
    float3 s, c;
    sincos(angles, s, c);

    float4x4 rx;
    rx[0] = float4(1.0, 0.0, 0.0, 0.0);
    rx[1] = float4(0.0, c.x, -s.x, 0.0);
    rx[2] = float4(0.0, s.x, c.x,  0.0);
    rx[3] = float4(0.0, 0.0, 0.0,  1.0);

    float4x4 ry;
    ry[0] = float4(c.y,  0.0, s.y, 0.0);
    ry[1] = float4(0.0,  1.0, 0.0, 0.0);
    ry[2] = float4(-s.y, 0.0, c.y, 0.0);
    ry[3] = float4(0.0,  0.0, 0.0, 1.0);

    float4x4 rz;
    rz[0] = float4(c.z, -s.z, 0.0, 0.0);
    rz[1] = float4(s.z, c.z,  0.0, 0.0);
    rz[2] = float4(0.0, 0.0,  1.0, 0.0);
    rz[3] = float4(0.0, 0.0,  0.0, 1.0);

    return mul(rz, mul(ry, rx));
}

static const half PI = 3.14159265359h;

half pow5(half x) {
    half x2 = x * x;
    return x2 * x2 * x;
}

half distributionGGX(half NdotH, half roughness) {
    half a = roughness * roughness;
    half a2 = a * a;
    half d = NdotH * NdotH * (a2 - 1.0h) + 1.0h;
    return a2 / (PI * d * d);
}

half geometrySchlickGGX(half NdotX, half k) {
    return NdotX / (NdotX * (1.0h - k) + k);
}

half geometrySmith(half NdotV, half NdotL, half roughness) {
    half k = (roughness + 1.0h) * (roughness + 1.0h) / 8.0h;
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

half3 fresnelSchlick(half VdotH, half3 Fzero) {
    return Fzero + (1.0h - Fzero) * pow5(saturate(1.0h - VdotH));
}

[[vk::binding(1, 0)]] Sampler2D texture;
[[vk::binding(0, 1)]] Sampler2D normalMap;
[[vk::binding(0, 2)]] Sampler2D roughnessMap;
[[vk::binding(0, 3)]] Sampler2D metallicMap;
[[vk::binding(0, 4)]] Sampler2D heightMap;

float2 parallaxOcclusionMapping(float2 texCoords, float3 viewDirTS, float viewDistance) {
    const float fadeStart = 10.0;
    const float fadeEnd = 100.0;

    float lodFactor = saturate((viewDistance - fadeStart) / (fadeEnd - fadeStart));

    if (lodFactor >= 1.0) {
        return texCoords;
    }

    const float baseHeightScale = 0.05;
    float currentHeightScale = lerp(baseHeightScale, 0.0, lodFactor);

    float maxLayers = lerp(32.0, 2.0, lodFactor);
    float minLayers = lerp(8.0, 2.0, lodFactor);

    float numLayers = lerp(maxLayers, minLayers, abs(dot(float3(0.0, 0.0, 1.0), viewDirTS)));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    float2 p = viewDirTS.xy / max(viewDirTS.z, 0.001) * currentHeightScale;
    float2 deltaTexCoords = p / numLayers;

    float2 currentTexCoords = texCoords;
    float currentDepthMapValue = 1.0 - heightMap.SampleLevel(currentTexCoords, 0.0).r;

    for (int i = 0; i < 32 && currentLayerDepth < currentDepthMapValue; i++) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = 1.0 - heightMap.SampleLevel(currentTexCoords, 0.0).r;
        currentLayerDepth += layerDepth;
    }

    float2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - heightMap.SampleLevel(prevTexCoords, 0.0).r) - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    return prevTexCoords * weight + currentTexCoords * (1.0 - weight);
}

[shader("vertex")]
VSOutput vertMainInstanced(InstancedVSInput input) {
    VSOutput output;
    float4x4 rot = eulerRotationMatrix(input.instanceRotation);
    float4 rotatedPos = mul(rot, float4(input.inPos, 1.0));
    float4 worldPos = float4(rotatedPos.xyz + input.instancePos, 1.0);

    output.pos = mul(ubo.proj, mul(ubo.view, worldPos));
    float3x3 rot3 = (float3x3)rot;
    
    output.fragNormal = (half3)normalize(mul(rot3, input.inNormal));
    output.fragTangent = (half3)normalize(mul(rot3, input.inTangent));
    output.fragColor = (half3)input.inColor;
    output.fragTexCoord = input.inTexCoord;
    
    output.viewDiff = ubo.cameraPos.xyz - worldPos.xyz;

    return output;
}

[shader("fragment")]
half4 fragMain(VSOutput vertIn) : SV_TARGET {
    float viewDistance = length(vertIn.viewDiff);
    float3 V = vertIn.viewDiff / viewDistance;

    half3 N = normalize(vertIn.fragNormal);
    half3 T = normalize(vertIn.fragTangent);
    T = normalize(T - N * dot(N, T));
    half3 B = cross(N, T);
    half3x3 TBN = half3x3(T, B, N);

    float3 viewDirTS = normalize(mul((float3x3)TBN, V));

    float2 texCoord = parallaxOcclusionMapping(vertIn.fragTexCoord, viewDirTS, viewDistance);

    half4 texColor = (half4)texture.Sample(texCoord);
    if (texColor.a < 0.5)
        discard;

    half3 tangentNormal = (half3)normalMap.Sample(texCoord).rgb * 2.0h - 1.0h;
    N = normalize(mul(tangentNormal, TBN));

    const half3 L = half3(0.57735h, 0.57735h, 0.57735h);
    half3 halfV = (half3)V; 
    half3 H = normalize(L + halfV);

    half NdotL = saturate(dot(N, L));
    half NdotV = saturate(dot(N, halfV));
    half LdotH = saturate(dot(L, H));
    half NdotH = saturate(dot(N, H));

    half roughness = (half)roughnessMap.Sample(texCoord).r;

    half fd90 = 0.5h + 2.0h * roughness * LdotH * LdotH;
    half lightScatter = 1.0h + (fd90 - 1.0h) * pow5(saturate(1.0h - NdotL));
    half viewScatter = 1.0h + (fd90 - 1.0h) * pow5(saturate(1.0h - NdotV));
    half burleyDiffuse = lightScatter * viewScatter * NdotL;

    half metallic = (half)metallicMap.Sample(texCoord).r;
    const half3 Fzero = lerp(half3(0.04h, 0.04h, 0.04h), texColor.rgb, metallic);

    half D = distributionGGX(NdotH, roughness);
    half G = geometrySmith(NdotV, NdotL, roughness);
    half3 F = fresnelSchlick(saturate(dot(halfV, H)), Fzero);
    
    half3 specular = (D * G * F) / max(4.0h * NdotV * NdotL, (half)0.001);

    const half3 skyColor = half3(0.35h, 0.38h, 0.42h);
    const half3 groundColor = half3(0.15h, 0.13h, 0.11h);
    const half ambientDiffuseIntensity = 0.2h;
    const half ambientSpecularIntensity = 0.1h;

    half skyFactor = N.z * 0.5h + 0.5h;
    half3 ambientDiffuseColor = lerp(groundColor, skyColor, skyFactor) * ambientDiffuseIntensity;
    half3 ambientSpecular = skyColor * fresnelSchlick(NdotV, Fzero) * ambientSpecularIntensity;

    half3 diffuseColor = texColor.rgb * (1.0h - metallic);
    half3 directLighting = diffuseColor * burleyDiffuse + specular * NdotL;
    half3 ambientLighting = diffuseColor * ambientDiffuseColor + ambientSpecular;
    half3 finalColor = directLighting + ambientLighting;

    return half4(finalColor, 1.0h);
}

[shader("fragment")]
half4 fragMainXray(VSOutput vertIn) : SV_TARGET {
    return half4(1.0h, 0.0h, 0.0h, 0.35h);
}
