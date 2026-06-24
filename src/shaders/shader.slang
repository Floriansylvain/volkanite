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
    float4x4 lightViewProj;
    float4 lightDir;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 fragTangent;
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

float3 fresnelSchlick(float VdotH, float3 Fzero) {
    return Fzero + (1.0 - Fzero) * pow5(saturate(1.0 - VdotH));
}

[[vk::binding(1, 0)]] Sampler2D texture;
[[vk::binding(2, 0)]] Sampler2D normalMap;
[[vk::binding(3, 0)]] Sampler2D ormMap; // R=roughness G=metallic B=height
[[vk::binding(4, 0)]] Sampler2D shadowMap;

float2 parallaxOcclusionMapping(float2 texCoords, float3 viewDirTS, float viewDistance) {
    const float fadeStart = 10.0;
    const float fadeEnd = 100.0;

    float lodFactor = saturate((viewDistance - fadeStart) / (fadeEnd - fadeStart));

    if (lodFactor >= 1.0) {
        return texCoords;
    }

    const float baseHeightScale = 0.05;
    float currentHeightScale = lerp(baseHeightScale, 0.0, lodFactor);

    float maxLayers = lerp(12.0, 2.0, lodFactor);
    float minLayers = lerp(4.0, 2.0, lodFactor);

    float numLayers = lerp(maxLayers, minLayers, abs(dot(float3(0.0, 0.0, 1.0), viewDirTS)));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    float2 p = viewDirTS.xy / max(viewDirTS.z, 0.001) * currentHeightScale;
    float2 deltaTexCoords = p / numLayers;

    float2 currentTexCoords = texCoords;
    float currentDepthMapValue = 1.0 - ormMap.SampleLevel(currentTexCoords, 0.0).b;

    for (int i = 0; i < 32 && currentLayerDepth < currentDepthMapValue; i++) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = 1.0 - ormMap.SampleLevel(currentTexCoords, 0.0).b;
        currentLayerDepth += layerDepth;
    }

    float2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - ormMap.SampleLevel(prevTexCoords, 0.0).b) - currentLayerDepth + layerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    return prevTexCoords * weight + currentTexCoords * (1.0 - weight);
}

float computeShadow(float3 worldPos, float3 N, float3 L) {
    float4 shadowClip = mul(ubo.lightViewProj, float4(worldPos, 1.0));
    float3 shadowNDC = shadowClip.xyz / shadowClip.w;
    float2 shadowUV = shadowNDC.xy * 0.5 + 0.5;
    float receiverDepth = shadowNDC.z;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0 || receiverDepth < 0.0 ||
        receiverDepth > 1.0) {
        return 1.0; 
    }

    const float bias = max(0.0015 * (1.0 - dot(N, L)), 0.00025);
    const float texelSize = ubo.lightDir.w;

    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float sampledDepth = shadowMap.Sample(shadowUV + float2(x, y) * texelSize).r;
            shadow += (receiverDepth - bias) > sampledDepth ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
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

    output.viewDiff = ubo.cameraPos.xyz - worldPos.xyz;

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float viewDistance = length(vertIn.viewDiff);
    float3 V = vertIn.viewDiff / viewDistance;

    float3 N = normalize(vertIn.fragNormal);
    float3 T = normalize(vertIn.fragTangent);
    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    float3 viewDirTS = normalize(mul((float3x3)TBN, V));

    float2 texCoord = parallaxOcclusionMapping(vertIn.fragTexCoord, viewDirTS, viewDistance);

    float4 texColor = texture.Sample(texCoord);

    if (texColor.a < 0.5)
        discard;

    float3 tangentNormal = normalMap.Sample(texCoord).rgb * 2.0 - 1.0;
    N = normalize(mul(tangentNormal, TBN));

    const float3 L = normalize(ubo.lightDir.xyz);
    float3 H = normalize(L + V);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float LdotH = saturate(dot(L, H));
    float NdotH = saturate(dot(N, H));

    float3 worldPos = ubo.cameraPos.xyz - vertIn.viewDiff;
    float shadow = computeShadow(worldPos, N, L);

    float3 orm = ormMap.Sample(texCoord).rgb;
    float roughness = orm.r;

    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotL));
    float viewScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotV));
    float burleyDiffuse = lightScatter * viewScatter * NdotL;

    float metallic = orm.g;
    float3 Fzero = lerp(float3(0.04, 0.04, 0.04), texColor.rgb, metallic);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    float3 F = fresnelSchlick(saturate(dot(V, H)), Fzero);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    const float3 skyColor = float3(0.35, 0.38, 0.42);
    const float3 groundColor = float3(0.15, 0.13, 0.11);

    const float ambientDiffuseIntensity = 0.2;
    const float ambientSpecularIntensity = 0.1;

    float skyFactor = N.z * 0.5 + 0.5;

    float3 ambientDiffuseColor = lerp(groundColor, skyColor, skyFactor) * ambientDiffuseIntensity;
    float3 ambientSpecular = skyColor * fresnelSchlick(NdotV, Fzero) * ambientSpecularIntensity;

    float3 diffuseColor = texColor.rgb * (1.0 - metallic);

    float3 directLighting = (diffuseColor * burleyDiffuse + specular * NdotL) * shadow;
    float3 ambientLighting = diffuseColor * ambientDiffuseColor + ambientSpecular;

    float3 finalColor = directLighting + ambientLighting;

    return float4(finalColor, 1.0);
    //return float4(shadow, shadow, shadow, 1.0);
}

[shader("fragment")]
float4 fragMainXray(VSOutput vertIn) : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 0.35);
}

struct ShadowVSOutput {
    float4 pos : SV_Position;
    float2 fragTexCoord;
};

[shader("vertex")]
ShadowVSOutput vertShadowInstanced(InstancedVSInput input) {
    ShadowVSOutput output;
    float4x4 rot = eulerRotationMatrix(input.instanceRotation);
    float4 rotatedPos = mul(rot, float4(input.inPos, 1.0));
    float4 worldPos = float4(rotatedPos.xyz + input.instancePos, 1.0);

    output.pos = mul(ubo.lightViewProj, worldPos);
    output.fragTexCoord = input.inTexCoord;

    return output;
}

[shader("fragment")]
void fragShadow(ShadowVSOutput vertIn) {
    float alpha = texture.Sample(vertIn.fragTexCoord).a;
    if (alpha < 0.5)
        discard;
}
