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
    float2 chunkOrigin;
    float chunkWorldSize;
    float textureWorldScale;
    float gridDim;
    float uvScaleX;
    float uvScaleY;
    float skirtDepth;
    int layerCount;
};

[[vk::push_constant]] TerrainPushConstants terrainPC;

struct TerrainLayerParams {
    float preferredHeight;
    float heightRange;
    float preferredSlope;
    float slopeRange;
};

[[vk::binding(11, 0)]] StructuredBuffer<TerrainLayerParams> terrainLayers;
[[vk::binding(0, 1)]] Sampler2D terrainHeightMap;
[[vk::binding(1, 1)]] Sampler2D terrainNormalMap;

float4 terrainLayerWeights(float height, float slope) {
    float4 rawWeight = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < terrainPC.layerCount; i++) {
        TerrainLayerParams layer = terrainLayers[i];
        float heightTerm = (height - layer.preferredHeight) / max(layer.heightRange, 0.001);
        float slopeTerm = (slope - layer.preferredSlope) / max(layer.slopeRange, 0.001);
        float w = exp(-(heightTerm * heightTerm) - (slopeTerm * slopeTerm));

        if (i == 0) rawWeight.x = w;
        else if (i == 1) rawWeight.y = w;
        else if (i == 2) rawWeight.z = w;
        else rawWeight.w = w;
    }

    float sum = max(rawWeight.x + rawWeight.y + rawWeight.z + rawWeight.w, 0.0001);
    return rawWeight / sum;
}

float2 terrainChunkUV(float2 worldXY) {
    return (worldXY - terrainPC.chunkOrigin) / terrainPC.chunkWorldSize + float2(0.5, 0.5);
}

struct TerrainVSInput {
    float3 inPos;
    float3 inColor;
    float2 inTexCoord;
    float3 inNormal;
    float3 inTangent;
};

struct TerrainWorldSample {
    float2 worldXY;
    float height;
};

TerrainWorldSample terrainSampleWorld(float2 gridCoord) {
    float2 worldXY = terrainPC.chunkOrigin - float2(terrainPC.chunkWorldSize, terrainPC.chunkWorldSize) * 0.5 +
                     (gridCoord / terrainPC.gridDim) * terrainPC.chunkWorldSize;

    TerrainWorldSample result;
    result.worldXY = worldXY;
    result.height = terrainHeightMap.SampleLevel(terrainChunkUV(worldXY), 0.0).r;
    return result;
}

struct VSOutput {
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
    float3 fragNormal;
    float3 fragTangent;
    float3 viewDiff;
    float2 uvScale;
    float4 layerWeights;
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

[[vk::binding(8, 0)]] Sampler2D layerAlbedo[4];
[[vk::binding(9, 0)]] Sampler2D layerNormalMap[4];
[[vk::binding(10, 0)]] Sampler2D layerOrmMap[4];

float2 parallaxOcclusionMapping(Sampler2D heightTex, float2 texCoords, float3 viewDirTS, float viewDistance) {
    const float fadeStart = 10.0;
    const float fadeEnd = 100.0;

    if (viewDistance >= fadeEnd) {
        return texCoords;
    }

    static const float MIN_LAYERS = 4.0;
    static const float MAX_LAYERS = 12.0;
    const float heightScale = 0.05;

    float numLayers = round(lerp(MAX_LAYERS, MIN_LAYERS, abs(viewDirTS.z)));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    float2 p = viewDirTS.xy / max(viewDirTS.z, 0.05) * heightScale;
    const float maxParallaxOffset = heightScale * 2.0;

    float pLength = length(p);
    if (pLength > maxParallaxOffset) {
        p *= maxParallaxOffset / pLength;
    }

    float2 deltaTexCoords = p / numLayers;
    float2 currentTexCoords = texCoords;
    float currentDepthMapValue = 1.0 - heightTex.SampleLevel(currentTexCoords, 0.0).b;

    for (int i = 0; i < int(MAX_LAYERS) && currentLayerDepth < currentDepthMapValue; i++) {
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = 1.0 - heightTex.SampleLevel(currentTexCoords, 0.0).b;
        currentLayerDepth += layerDepth;
    }

    float2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = (1.0 - heightTex.SampleLevel(prevTexCoords, 0.0).b) - currentLayerDepth + layerDepth;
    float depthDelta = afterDepth - beforeDepth;
    float weight = abs(depthDelta) > 0.0001 ? afterDepth / depthDelta : 0.0;

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
    output.layerWeights = float4(0.0, 0.0, 0.0, 0.0);

    return output;
}

[shader("vertex")]
VSOutput vertTerrain(TerrainVSInput input) {
    VSOutput output;

    TerrainWorldSample center = terrainSampleWorld(input.inPos.xy);

    float3 normal = normalize(terrainNormalMap.SampleLevel(terrainChunkUV(center.worldXY), 0.0).rgb * 2.0 - 1.0);
    float3 tangent = normalize(float3(1.0, 0.0, 0.0) - normal * dot(normal, float3(1.0, 0.0, 0.0)));

    float skirtOffset = input.inPos.z > 0.5 ? terrainPC.skirtDepth : 0.0;
    float3 worldPos = float3(center.worldXY, center.height - skirtOffset);

    output.pos = mul(ubo.proj, mul(ubo.view, float4(worldPos, 1.0)));
    output.fragNormal = normal;
    output.fragTangent = tangent;
    output.fragColor = float3(1.0, 1.0, 1.0);
    output.fragTexCoord = center.worldXY / terrainPC.textureWorldScale;
    output.viewDiff = ubo.cameraPos.xyz - worldPos;
    output.uvScale = float2(terrainPC.uvScaleX, terrainPC.uvScaleY);
    output.layerWeights = terrainLayerWeights(center.height, 1.0 - normal.z);

    return output;
}

float3 evaluatePBR(float3 N, float3 V, float3 L, float3 diffuseColor, float roughness, float metallic, float shadow) {
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float LdotH = saturate(dot(L, H));
    float NdotH = saturate(dot(N, H));

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

    return directLighting + ambientLighting;
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

    float2 dx = ddx(scaledTexCoord);
    float2 dy = ddy(scaledTexCoord);

    float2 texCoord = parallaxOcclusionMapping(ormMap, scaledTexCoord, viewDirTS, viewDistance);

    float4 texColor = texture.SampleGrad(texCoord, dx, dy);
    if (texColor.a < 0.5)
        discard;

    float3 tangentNormal = normalMap.SampleGrad(texCoord, dx, dy).rgb * 2.0 - 1.0;
    N = normalize(mul(tangentNormal, TBN));

    const float3 L = normalize(ubo.lightDir.xyz);
    float3 worldPos = ubo.cameraPos.xyz - vertIn.viewDiff;
    float shadow = computeShadow(worldPos, N, L, vertIn.pos.xy);

    float3 orm = ormMap.SampleGrad(texCoord, dx, dy).rgb;
    float roughness = max(orm.r, 0.04);
    float metallic = orm.g;

    float3 finalColor = evaluatePBR(N, V, L, texColor.rgb, roughness, metallic, shadow);

    return float4(finalColor, 1.0);
}

float3 terrainTriplanarWeights(float3 N) {
    float3 blend = pow(abs(N), 4.0);
    return blend / max(blend.x + blend.y + blend.z, 0.0001);
}

struct TerrainTriplanarUVs {
    float2 uvX;
    float2 uvY;
    float2 uvZ;
};

TerrainTriplanarUVs terrainBaseUVs(float3 worldPos, float2 uvScale, float texScale) {
    TerrainTriplanarUVs uvs;
    uvs.uvX = worldPos.yz / texScale * uvScale;
    uvs.uvY = worldPos.xz / texScale * uvScale;
    uvs.uvZ = worldPos.xy / texScale * uvScale;
    return uvs;
}

static const float TRIPLANAR_AXIS_EPSILON = 0.04;

float4 sampleTriplanarStandard(Sampler2D tex, TerrainTriplanarUVs uvs, float3 weights, TerrainTriplanarUVs dx, TerrainTriplanarUVs dy) {
    float4 col = float4(0.0, 0.0, 0.0, 0.0);

    if (weights.x > TRIPLANAR_AXIS_EPSILON) col += tex.SampleGrad(uvs.uvX, dx.uvX, dy.uvX) * weights.x;
    if (weights.y > TRIPLANAR_AXIS_EPSILON) col += tex.SampleGrad(uvs.uvY, dx.uvY, dy.uvY) * weights.y;
    if (weights.z > TRIPLANAR_AXIS_EPSILON) col += tex.SampleGrad(uvs.uvZ, dx.uvZ, dy.uvZ) * weights.z;

    return col;
}

float3 sampleTriplanarNormal(Sampler2D tex, TerrainTriplanarUVs uvs, float3 N, float3 weights, TerrainTriplanarUVs dx, TerrainTriplanarUVs dy) {
    float3 result = float3(0.0, 0.0, 0.0);

    if (weights.x > TRIPLANAR_AXIS_EPSILON) {
        float3 ts = tex.SampleGrad(uvs.uvX, dx.uvX, dy.uvX).rgb * 2.0 - 1.0;
        result += float3(N.x, ts.x + N.y, ts.y + N.z) * weights.x;
    }
    if (weights.y > TRIPLANAR_AXIS_EPSILON) {
        float3 ts = tex.SampleGrad(uvs.uvY, dx.uvY, dy.uvY).rgb * 2.0 - 1.0;
        result += float3(ts.x + N.x, N.y, ts.y + N.z) * weights.y;
    }
    if (weights.z > TRIPLANAR_AXIS_EPSILON) {
        float3 ts = tex.SampleGrad(uvs.uvZ, dx.uvZ, dy.uvZ).rgb * 2.0 - 1.0;
        result += float3(ts.x + N.x, ts.y + N.y, N.z) * weights.z;
    }

    return normalize(result);
}

struct TerrainLayerSample {
    float4 albedo;
    float3 orm;
    float3 worldNormal;
};

TerrainLayerSample sampleTerrainLayer(Sampler2D albedoTex, Sampler2D normalTex, Sampler2D ormTex, float3 N,
                                      TerrainTriplanarUVs uvs, float3 triWeights,
                                      TerrainTriplanarUVs dx, TerrainTriplanarUVs dy) {
    TerrainLayerSample result;

    result.albedo = sampleTriplanarStandard(albedoTex, uvs, triWeights, dx, dy);
    result.worldNormal = sampleTriplanarNormal(normalTex, uvs, N, triWeights, dx, dy);
    result.orm = sampleTriplanarStandard(ormTex, uvs, triWeights, dx, dy).rgb;

    return result;
}

[shader("fragment")]
float4 fragTerrain(VSOutput vertIn) : SV_TARGET {
    float viewDistance = length(vertIn.viewDiff);
    float3 V = vertIn.viewDiff / viewDistance;

    float3 N = normalize(vertIn.fragNormal);
    float3 worldPos = ubo.cameraPos.xyz - vertIn.viewDiff;

    float4 weights = vertIn.layerWeights;
    float3 triWeights = terrainTriplanarWeights(N);

    TerrainTriplanarUVs baseUVs = terrainBaseUVs(worldPos, vertIn.uvScale, terrainPC.textureWorldScale);

    TerrainTriplanarUVs dx;
    dx.uvX = ddx(baseUVs.uvX); dx.uvY = ddx(baseUVs.uvY); dx.uvZ = ddx(baseUVs.uvZ);
    TerrainTriplanarUVs dy;
    dy.uvX = ddy(baseUVs.uvX); dy.uvY = ddy(baseUVs.uvY); dy.uvZ = ddy(baseUVs.uvZ);

    float4 texColor = float4(0.0, 0.0, 0.0, 0.0);
    float3 totalNormal = float3(0.0, 0.0, 0.0);
    float3 totalOrm = float3(0.0, 0.0, 0.0);

    if (weights.x > 0.01) {
        TerrainLayerSample s = sampleTerrainLayer(layerAlbedo[0], layerNormalMap[0], layerOrmMap[0], N, baseUVs, triWeights, dx, dy);
        texColor += s.albedo * weights.x;
        totalNormal += s.worldNormal * weights.x;
        totalOrm += s.orm * weights.x;
    }
    if (weights.y > 0.01) {
        TerrainLayerSample s = sampleTerrainLayer(layerAlbedo[1], layerNormalMap[1], layerOrmMap[1], N, baseUVs, triWeights, dx, dy);
        texColor += s.albedo * weights.y;
        totalNormal += s.worldNormal * weights.y;
        totalOrm += s.orm * weights.y;
    }
    if (weights.z > 0.01) {
        TerrainLayerSample s = sampleTerrainLayer(layerAlbedo[2], layerNormalMap[2], layerOrmMap[2], N, baseUVs, triWeights, dx, dy);
        texColor += s.albedo * weights.z;
        totalNormal += s.worldNormal * weights.z;
        totalOrm += s.orm * weights.z;
    }
    if (weights.w > 0.01) {
        TerrainLayerSample s = sampleTerrainLayer(layerAlbedo[3], layerNormalMap[3], layerOrmMap[3], N, baseUVs, triWeights, dx, dy);
        texColor += s.albedo * weights.w;
        totalNormal += s.worldNormal * weights.w;
        totalOrm += s.orm * weights.w;
    }

    if (texColor.a < 0.5)
        discard;

    N = normalize(totalNormal);
    float3 orm = totalOrm;

    const float3 L = normalize(ubo.lightDir.xyz);
    float shadow = computeShadow(worldPos, N, L, vertIn.pos.xy);

    float roughness = max(orm.r, 0.04);
    float metallic = orm.g;

    float3 finalColor = evaluatePBR(N, V, L, texColor.rgb, roughness, metallic, shadow);

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

    TerrainWorldSample center = terrainSampleWorld(input.inPos.xy);
    float skirtOffset = input.inPos.z > 0.5 ? terrainPC.skirtDepth : 0.0;
    float3 worldPos = float3(center.worldXY, center.height - skirtOffset);

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

[shader("fragment")]
void fragTerrainShadow(ShadowVSOutput vertIn) {
}
