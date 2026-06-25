struct InstancedVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
    float3 inTangent;
    float3 instancePos;
    float3 instanceRotation;
    float2 instanceUVScale;
};

struct CameraUniform {
    float4x4 view;
    float4x4 proj;
    float4 cameraPos;
    float4x4 lightViewProj;
    float4 lightDir;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> ubo;

struct TerrainPushConstants {
    float2 noiseOffset;
    float noiseScale;
    float heightScale;
    float baseHeight;
    float persistence;
    float lacunarity;
    int octaves;
    float textureWorldScale;
    float gridDim;
    float uvScaleX;
    float uvScaleY;
    float ridgeSharpness;
    float heightRedistribution;
    float regionScale;
    float regionThreshold;
    float regionBlendWidth;
};

[[vk::push_constant]] TerrainPushConstants terrainPC;

float2 terrainNoiseGradient(float2 p) {
    float2 hashed = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(hashed) * 43758.5453123);
}

float terrainQuinticFade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float terrainGradientNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    float2 g00 = terrainNoiseGradient(i + float2(0.0, 0.0));
    float2 g10 = terrainNoiseGradient(i + float2(1.0, 0.0));
    float2 g01 = terrainNoiseGradient(i + float2(0.0, 1.0));
    float2 g11 = terrainNoiseGradient(i + float2(1.0, 1.0));

    float v00 = dot(g00, f - float2(0.0, 0.0));
    float v10 = dot(g10, f - float2(1.0, 0.0));
    float v01 = dot(g01, f - float2(0.0, 1.0));
    float v11 = dot(g11, f - float2(1.0, 1.0));

    float u = terrainQuinticFade(f.x);
    float v = terrainQuinticFade(f.y);

    float x1 = lerp(v00, v10, u);
    float x2 = lerp(v01, v11, u);
    return lerp(x1, x2, v);
}

float terrainRegionMask(float2 worldPos) {
    float2 base = worldPos / max(terrainPC.regionScale, 1.0);
    float n = saturate(terrainGradientNoise2D(base) * 0.5 + 0.5);
    float halfWidth = max(terrainPC.regionBlendWidth, 0.001);
    return smoothstep(terrainPC.regionThreshold - halfWidth, terrainPC.regionThreshold + halfWidth, n);
}

float terrainHeight(float2 worldPos) {
    float amplitude = 1.0;
    float frequency = 1.0;
    float hills = 0.0;
    float ridged = 0.0;
    float2 base = worldPos / terrainPC.noiseScale + terrainPC.noiseOffset;
    float ridgeExponent = 2.0 + terrainPC.ridgeSharpness * 4.0;

    for (int i = 0; i < terrainPC.octaves; i++) {
        float n = terrainGradientNoise2D(base * frequency);
        hills += n * amplitude;
        ridged += pow(saturate(1.0 - abs(n)), ridgeExponent) * amplitude;

        amplitude *= terrainPC.persistence;
        frequency *= terrainPC.lacunarity;
    }

    float mountainAmount = terrainRegionMask(worldPos);
    float combined = lerp(hills, ridged, mountainAmount);

    float sign_h = combined < 0.0 ? -1.0 : 1.0;
    float h = sign_h * pow(abs(combined), max(terrainPC.heightRedistribution, 0.0001));

    return h * terrainPC.heightScale + terrainPC.baseHeight;
}

float2 terrainMorphGridCoord(float2 gridCoord, float morphT) {
    float2 oddMask = frac(gridCoord * 0.5) * 2.0;
    float2 evenGridCoord = gridCoord - oddMask;
    return lerp(gridCoord, evenGridCoord, morphT);
}

struct TerrainVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
    float3 inTangent;
    float2 instanceOrigin;
    float4 instanceParams;
};

struct TerrainWorldSample {
    float2 worldXY;
    float height;
};

TerrainWorldSample terrainSampleWorld(float2 gridCoord, float2 origin, float scale, float morphT) {
    float2 morphedGrid = terrainMorphGridCoord(gridCoord, morphT);
    float2 worldXY = origin + (morphedGrid / terrainPC.gridDim) * scale;

    TerrainWorldSample result;
    result.worldXY = worldXY;
    result.height = terrainHeight(worldXY);
    return result;
}

float terrainMorphFactor(float2 unmorphedWorldXY, float morphStart, float morphEnd) {
    float dist = length(ubo.cameraPos.xy - unmorphedWorldXY);
    return saturate((dist - morphStart) / max(morphEnd - morphStart, 0.0001));
}

struct VSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 fragTangent;
    float3 viewDiff;
    float2 uvScale;
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

float3 fresnelSchlickRoughness(float cosTheta, float3 Fzero, float roughness) {
    float3 r = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return Fzero + (max(r, Fzero) - Fzero) * pow5(saturate(1.0 - cosTheta));
}

[[vk::binding(1, 0)]] Sampler2D texture;
[[vk::binding(2, 0)]] Sampler2D normalMap;
[[vk::binding(3, 0)]] Sampler2D ormMap; 
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

float interleavedGradientNoise(float2 pixelCoord) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixelCoord, magic.xy)));
}

float computeShadow(float3 worldPos, float3 N, float3 L, float2 fragCoord) {
    float4 shadowClip = mul(ubo.lightViewProj, float4(worldPos, 1.0));
    float3 shadowNDC = shadowClip.xyz / shadowClip.w;
    float2 shadowUV = shadowNDC.xy * 0.5 + 0.5;
    float receiverDepth = shadowNDC.z;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0 || receiverDepth < 0.0 || receiverDepth > 1.0) {
        return 1.0;
    }

    const float bias = max(0.0015 * (1.0 - dot(N, L)), 0.00025);
    const float texelSize = ubo.lightDir.w;
    float shadow = 0.0;

    float2 poissonDisk[16] = {
        float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
        float2(-0.91588401, 0.45771432), float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
        float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
        float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
    };

    float spread = texelSize * 1.5;

    float noise = interleavedGradientNoise(fragCoord);
    float angle = noise * 2.0 * PI;
    float s, c;
    sincos(angle, s, c);
    float2x2 rotMatrix = float2x2(c, -s, s, c);

    for (int i = 0; i < 16; i++) {
        float2 rotatedOffset = mul(poissonDisk[i], rotMatrix);
        float sampledDepth = shadowMap.Sample(shadowUV + rotatedOffset * spread).r;
        
        shadow += (receiverDepth - bias) > sampledDepth ? 0.0 : 1.0;
    }

    return shadow / 16.0;
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
    output.uvScale = input.instanceUVScale;

    return output;
}

[shader("vertex")]
VSOutput vertTerrain(TerrainVSInput input) {
    VSOutput output;

    float2 origin = input.instanceOrigin;
    float scale = input.instanceParams.x;
    float morphStart = input.instanceParams.y;
    float morphEnd = input.instanceParams.z;

    float2 unmorphedWorldXY = origin + (input.inPos.xy / terrainPC.gridDim) * scale;
    float morphT = terrainMorphFactor(unmorphedWorldXY, morphStart, morphEnd);

    TerrainWorldSample center = terrainSampleWorld(input.inPos.xy, origin, scale, morphT);

    float eps = max(scale / terrainPC.gridDim * 0.5, 0.01);
    float hLx = terrainHeight(center.worldXY - float2(eps, 0.0));
    float hRx = terrainHeight(center.worldXY + float2(eps, 0.0));
    float hDy = terrainHeight(center.worldXY - float2(0.0, eps));
    float hUy = terrainHeight(center.worldXY + float2(0.0, eps));

    float3 normal = normalize(float3((hLx - hRx) / (2.0 * eps), (hDy - hUy) / (2.0 * eps), 1.0));
    float3 tangent = normalize(float3(1.0, 0.0, (hRx - hLx) / (2.0 * eps)));

    float3 worldPos = float3(center.worldXY, center.height);

    output.pos = mul(ubo.proj, mul(ubo.view, float4(worldPos, 1.0)));
    output.fragNormal = normal;
    output.fragTangent = tangent;
    output.fragColor = float3(1.0, 1.0, 1.0);
    output.fragTexCoord = center.worldXY / terrainPC.textureWorldScale;
    output.viewDiff = ubo.cameraPos.xyz - worldPos;
    output.uvScale = float2(terrainPC.uvScaleX, terrainPC.uvScaleY);

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
   
    float2 scaledTexCoord = vertIn.fragTexCoord * vertIn.uvScale;
    float2 texCoord = parallaxOcclusionMapping(scaledTexCoord, viewDirTS, viewDistance);

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
    
    float shadow = computeShadow(worldPos, N, L, vertIn.pos.xy);

    float3 orm = ormMap.Sample(texCoord).rgb;
    float roughness = max(orm.r, 0.04); 
    float metallic = orm.g;
    
    float3 diffuseColor = texColor.rgb;
    float3 Fzero = lerp(float3(0.04, 0.04, 0.04), diffuseColor, metallic);

    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotL));
    float viewScatter = 1.0 + (fd90 - 1.0) * pow5(saturate(1.0 - NdotV));
    float burleyDiffuse = lightScatter * viewScatter;

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    float3 F = fresnelSchlick(saturate(dot(V, H)), Fzero);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;

    float3 directLighting = (diffuseColor * burleyDiffuse * kD + specular) * NdotL * shadow;

    const float3 skyColor = float3(0.35, 0.38, 0.42);
    const float3 groundColor = float3(0.15, 0.13, 0.11);
    const float ambientDiffuseIntensity = 0.2;
    const float ambientSpecularIntensity = 0.1;

    float3 kS_ambient = fresnelSchlickRoughness(NdotV, Fzero, roughness);
    float3 kD_ambient = float3(1.0, 1.0, 1.0) - kS_ambient;
    kD_ambient *= 1.0 - metallic;

    float skyFactor = N.z * 0.5 + 0.5;
    float3 ambientDiffuseColor = lerp(groundColor, skyColor, skyFactor) * ambientDiffuseIntensity;

    float3 R = reflect(-V, N);
    float R_z = R.z * 0.5 + 0.5;
    float3 ambientReflectionColor = lerp(groundColor, skyColor, R_z);
    
    float specAtten = exp2(-roughness * 4.0);
    float3 ambientSpecular = ambientReflectionColor * kS_ambient * ambientSpecularIntensity * specAtten;

    float3 ambientLighting = diffuseColor * ambientDiffuseColor * kD_ambient + ambientSpecular;

    float3 finalColor = directLighting + ambientLighting;

    return float4(finalColor, 1.0);
}

[shader("fragment")]
float4 fragMainXray(VSOutput vertIn) : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 0.35);
}

struct ShadowVSOutput {
    float4 pos : SV_Position;
    float2 fragTexCoord;
    float2 uvScale;
};

[shader("vertex")]
ShadowVSOutput vertShadowInstanced(InstancedVSInput input) {
    ShadowVSOutput output;
    float4x4 rot = eulerRotationMatrix(input.instanceRotation);
    float4 rotatedPos = mul(rot, float4(input.inPos, 1.0));
    float4 worldPos = float4(rotatedPos.xyz + input.instancePos, 1.0);

    output.pos = mul(ubo.lightViewProj, worldPos);
    output.fragTexCoord = input.inTexCoord;
    output.uvScale = input.instanceUVScale;

    return output;
}

[shader("vertex")]
ShadowVSOutput vertTerrainShadow(TerrainVSInput input) {
    ShadowVSOutput output;

    float2 origin = input.instanceOrigin;
    float scale = input.instanceParams.x;
    float morphStart = input.instanceParams.y;
    float morphEnd = input.instanceParams.z;

    float2 unmorphedWorldXY = origin + (input.inPos.xy / terrainPC.gridDim) * scale;
    float morphT = terrainMorphFactor(unmorphedWorldXY, morphStart, morphEnd);

    TerrainWorldSample center = terrainSampleWorld(input.inPos.xy, origin, scale, morphT);
    float3 worldPos = float3(center.worldXY, center.height);

    output.pos = mul(ubo.lightViewProj, float4(worldPos, 1.0));
    output.fragTexCoord = center.worldXY / terrainPC.textureWorldScale;
    output.uvScale = float2(terrainPC.uvScaleX, terrainPC.uvScaleY);

    return output;
}

[shader("fragment")]
void fragShadow(ShadowVSOutput vertIn) {
    float2 scaledTexCoord = vertIn.fragTexCoord * vertIn.uvScale;
    float alpha = texture.Sample(scaledTexCoord).a;
    if (alpha < 0.5)
        discard;
}
