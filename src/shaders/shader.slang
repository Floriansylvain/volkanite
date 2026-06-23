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
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 fragTangent;
    float3 worldPos;
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

static const float PI = 3.14159265359;

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

float3 fresnelSchlick(float VdotH, float3 F0) {
    return F0 + (1.0 - F0) * pow5(saturate(1.0 - VdotH));
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
    output.fragNormal = normalize(mul(rot3, input.inNormal));
    output.fragTangent = normalize(mul(rot3, input.inTangent));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    output.worldPos = worldPos.xyz;

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float3 N = normalize(vertIn.fragNormal);
    float3 T = normalize(vertIn.fragTangent);
    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 viewDiff = ubo.cameraPos.xyz - vertIn.worldPos;
    float viewDistance = length(viewDiff);
    float3 V = viewDiff / viewDistance;

    float3 viewDirTS = normalize(mul(TBN, V));

    float2 texCoord = parallaxOcclusionMapping(vertIn.fragTexCoord, viewDirTS, viewDistance);

    float4 texColor = texture.Sample(texCoord);
    if (texColor.a < 0.5)
        discard;

    float3 tangentNormal = normalMap.Sample(texCoord).rgb * 2.0 - 1.0;
    N = normalize(mul(tangentNormal, TBN));

    const float3 L = float3(0.57735026, 0.57735026, 0.57735026);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float LdotH = saturate(dot(L, H));
    float NdotH = saturate(dot(N, H));

    float roughness = roughnessMap.Sample(texCoord).r;

    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotL));
    float viewScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotV));
    float burleyDiffuse = lightScatter * viewScatter * NdotL;

    float metallic = metallicMap.Sample(texCoord).r;
    const float3 F0 = lerp(float3(0.04, 0.04, 0.04), texColor.rgb, metallic);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    float3 F = fresnelSchlick(saturate(dot(V, H)), F0);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    const float3 skyColor = float3(0.35, 0.38, 0.42);
    const float3 groundColor = float3(0.15, 0.13, 0.11);
    const float ambientDiffuseIntensity = 0.2;
    const float ambientSpecularIntensity = 0.1;

    float skyFactor = N.z * 0.5 + 0.5;
    float3 ambientDiffuseColor = lerp(groundColor, skyColor, skyFactor) * ambientDiffuseIntensity;
    float3 ambientSpecular = skyColor * fresnelSchlick(NdotV, F0) * ambientSpecularIntensity;

    float3 diffuseColor = texColor.rgb * (1.0 - metallic);
    float3 directLighting = diffuseColor * burleyDiffuse + specular * NdotL;
    float3 ambientLighting = diffuseColor * ambientDiffuseColor + ambientSpecular;
    float3 finalColor = directLighting + ambientLighting;

    return float4(finalColor, 1.0);
}

[shader("fragment")]
float4 fragMainXray(VSOutput vertIn) : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 0.35);
}
