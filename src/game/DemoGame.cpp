#include "DemoGame.hpp"
#include "Engine.hpp"
#include "TerrainTypes.hpp"
#include <SDL3/SDL.h>

namespace {
constexpr glm::vec3 SHOWCASE_ORIGIN{15.0f, -5.0f, 10.0f};
} // namespace

void DemoGame::init(Engine &engine) {
    const Engine::FBXModel house = engine.createFBXModel("models/House_scene_01.fbx", ".png");
    engine.placeFBXModel(house, glm::vec3(0.f, 0.f, -300.f));

    const auto rockAlbedo = engine.loadTexture("textures/layered-cliff_albedo.png");
    const auto rockNormalMap = engine.loadNormalMap("textures/layered-cliff_normal-ogl.png");
    const auto rockOrmMap = engine.loadOrmMapFile("textures/layered-cliff_orm.dds");
    Material rockMaterial = {};
    rockMaterial.albedo = rockAlbedo;
    rockMaterial.normalMap = rockNormalMap;
    rockMaterial.ormMap = rockOrmMap;

    const auto grassAlbedo = engine.loadTexture("textures/grassy-meadow1_albedo.png");
    const auto grassNormalMap = engine.loadNormalMap("textures/grassy-meadow1_normal-ogl.png");
    const auto grassOrmMap = engine.loadOrmMapFile("textures/grassy-meadow1_orm.dds");
    // const auto cavernAlbedo    = engine.loadTexture("textures/cavern-walls_albedo.png");
    // const auto cavernNormalMap = engine.loadNormalMap("textures/cavern-walls_normal-ogl.png");
    // const auto cavernOrmMap    = engine.loadOrmMap("textures/cavern-walls_roughness.png",
    // "textures/cavern-walls_metallic.png",
    //                                                "textures/cavern-walls_height.png");
    // Material cavernMaterial = {};
    // cavernMaterial.albedo    = cavernAlbedo;
    // cavernMaterial.normalMap = cavernNormalMap;
    // cavernMaterial.ormMap    = cavernOrmMap;

    Material grassMaterial = {};
    grassMaterial.albedo = grassAlbedo;
    grassMaterial.normalMap = grassNormalMap;
    grassMaterial.ormMap = grassOrmMap;

    const auto snowAlbedo = engine.loadTexture("textures/snow-packed12_albedo.png");
    const auto snowNormalMap = engine.loadNormalMap("textures/snow-packed12_normal-ogl.png");
    const auto snowOrmMap = engine.loadOrmMapFile("textures/snow-packed12_orm.dds");
    Material snowMaterial = {};
    snowMaterial.albedo = snowAlbedo;
    snowMaterial.normalMap = snowNormalMap;
    snowMaterial.ormMap = snowOrmMap;

    const auto sandAlbedo = engine.loadTexture("textures/sand-dunes1_albedo.png");
    const auto sandNormalMap = engine.loadNormalMap("textures/sand-dunes1_normal-ogl.png");
    const auto sandOrmMap = engine.loadOrmMapFile("textures/sand-dunes1_orm.dds");
    Material sandMaterial = {};
    sandMaterial.albedo = sandAlbedo;
    sandMaterial.normalMap = sandNormalMap;
    sandMaterial.ormMap = sandOrmMap;

    const auto tundraAlbedo = engine.loadTexture("textures/mossy-mud1_albedo.png");
    const auto tundraNormalMap = engine.loadNormalMap("textures/mossy-mud1_normal-ogl.png");
    const auto tundraOrmMap = engine.loadOrmMapFile("textures/mossy-mud1_orm.dds");
    Material tundraMaterial = {};
    tundraMaterial.albedo = tundraAlbedo;
    tundraMaterial.normalMap = tundraNormalMap;
    tundraMaterial.ormMap = tundraOrmMap;

    {
        TerrainConfig terrainConfig;
        terrainConfig.origin = glm::vec2(0.0f, 0.0f);
        terrainConfig.rootSize = 262144.f;
        terrainConfig.maxDepth = 9;
        terrainConfig.fineChunkResolution = 64;
        terrainConfig.chunkResolution = 33;
        terrainConfig.splitFactor = 2.0f;
        terrainConfig.textureWorldScale = 32.0f;
        terrainConfig.uvScale = glm::vec2(8.f);
        terrainConfig.noise.scale = 4000.f;
        terrainConfig.noise.heightScale = 1400.0f;
        terrainConfig.noise.baseHeight = -100.0f;
        terrainConfig.noise.octaves = 9;
        terrainConfig.noise.persistence = 0.48f;
        terrainConfig.noise.lacunarity = 2.0f;
        terrainConfig.noise.ridgeSharpness = 0.50f;
        terrainConfig.noise.heightRedistribution = 1.7f;
        terrainConfig.noise.regionScale = 7000.0f;
        terrainConfig.noise.regionThreshold = 0.50f;
        terrainConfig.noise.regionBlendWidth = 0.20f;
        terrainConfig.noise.flatScale = 3500.0f;
        terrainConfig.noise.flatThreshold = 0.38f;
        terrainConfig.noise.flatBlendWidth = 0.20f;
        terrainConfig.noise.minRelief = 0.14f;

        terrainConfig.moistureScale = 5200.0f;
        terrainConfig.moistureOffset = glm::vec2(31100.f, -7700.f);

        terrainConfig.morphRatio = 0.15f;
        terrainConfig.biomeLayers.push_back(TerrainBiomeLayer{
            .material = grassMaterial,
            .preferredHeight = 80.0f,
            .heightRange = 320.0f,
            .preferredSlope = 0.0f,
            .slopeRange = 0.45f,
            .preferredMoisture = 0.65f,
            .moistureRange = 0.45f,
            .textureScale = 1.0f,
            .patchiness = 0.3f,
            .patchScale = 450.0f,
            .blendSharpness = 1.1f,
        });
        terrainConfig.biomeLayers.push_back(TerrainBiomeLayer{
            .material = sandMaterial,
            .preferredHeight = 30.0f,
            .heightRange = 250.0f,
            .preferredSlope = 0.0f,
            .slopeRange = 0.35f,
            .preferredMoisture = 0.22f,
            .moistureRange = 0.28f,
            .textureScale = 1.3f,
            .patchiness = 0.5f,
            .patchScale = 700.0f,
            .blendSharpness = 1.0f,
        });
        terrainConfig.biomeLayers.push_back(TerrainBiomeLayer{
            .material = rockMaterial,
            .preferredHeight = 400.0f,
            .heightRange = 800.0f,
            .preferredSlope = 0.55f,
            .slopeRange = 0.38f,
            .preferredMoisture = 0.5f,
            .moistureRange = 2.0f,
            .textureScale = 0.85f,
            .patchiness = 0.1f,
            .patchScale = 450.0f,
            .blendSharpness = 1.7f,
        });
        terrainConfig.biomeLayers.push_back(TerrainBiomeLayer{
            .material = snowMaterial,
            .preferredHeight = 900.0f,
            .heightRange = 350.0f,
            .preferredSlope = 0.05f,
            .slopeRange = 0.6f,
            .preferredMoisture = 0.5f,
            .moistureRange = 2.0f,
            .textureScale = 1.1f,
            .patchiness = 0.3f,
            .patchScale = 1000.0f,
            .blendSharpness = 1.4f,
        });
        terrainConfig.biomeLayers.push_back(TerrainBiomeLayer{
            .material = tundraMaterial,
            .preferredHeight = 600.0f,
            .heightRange = 320.0f,
            .preferredSlope = 0.05f,
            .slopeRange = 0.3f,
            .preferredMoisture = 0.25f,
            .moistureRange = 0.35f,
            .textureScale = 1.1f,
            .patchiness = 0.5f,
            .patchScale = 650.0f,
            .blendSharpness = 1.0f,
        });

        engine.createTerrain(terrainConfig);
    }

    auto cubeMesh = engine.createCubeMesh(1.f);
    cubeMesh->uvScale = glm::vec2(1.f);

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = rockMaterial;
        obj.position = SHOWCASE_ORIGIN + glm::vec3(0.f, -6.f, 2.f);
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.emplace_back(handle, glm::vec3(0.6f, 1.0f, 1.4f));
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = rockMaterial;
        obj.position = SHOWCASE_ORIGIN;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        orbitingObjects.emplace_back(handle, SHOWCASE_ORIGIN, 6.0f, 1.2f, 0.0f);
    }

    {
        const glm::vec3 center = SHOWCASE_ORIGIN + glm::vec3(0.f, 6.f, -2.f);
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = rockMaterial;
        obj.position = center;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.emplace_back(handle, glm::vec3(2.0f, -1.5f, 0.8f));
        orbitingObjects.emplace_back(handle, center, 4.0f, -0.8f, 0.0f);
    }
}

void DemoGame::update(Engine &engine, const float deltaTime) {
    for (const auto &spinner : spinningObjects) {
        engine.getRenderObject(spinner.handle).rotation.z += spinner.spinSpeed.z * deltaTime;
    }

    for (auto &orbiter : orbitingObjects) {
        orbiter.angle += orbiter.speed * deltaTime;
        engine.getRenderObject(orbiter.handle).position =
            orbiter.center + glm::vec3(cos(orbiter.angle) * orbiter.radius, sin(orbiter.angle) * orbiter.radius, 0.f);
    }

    Camera &camera = engine.getCamera();

    float mouseDx;
    float mouseDy;
    SDL_GetRelativeMouseState(&mouseDx, &mouseDy);

    constexpr float sensitivity = 0.1f;
    camera.yaw -= mouseDx * sensitivity;
    camera.pitch -= mouseDy * sensitivity;

    constexpr double maxPitch = 89.0f;
    camera.pitch = std::clamp(camera.pitch, -maxPitch, maxPitch);

    const float yawRad = glm::radians(camera.yaw);
    const glm::vec3 flatForward(cos(yawRad), sin(yawRad), 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f, 0.0f, 1.0f)));

    const bool *key_states = SDL_GetKeyboardState(nullptr);
    glm::vec3 input = {0.f, 0.f, 0.f};
    float speed = 1000.0f;

    if (key_states[SDL_SCANCODE_LSHIFT])
        speed *= 5.f;
    if (key_states[SDL_SCANCODE_LALT])
        speed *= 0.05f;
    if (key_states[SDL_SCANCODE_W])
        input.y += 1.f;
    if (key_states[SDL_SCANCODE_S])
        input.y -= 1.f;
    if (key_states[SDL_SCANCODE_D])
        input.x += 1.f;
    if (key_states[SDL_SCANCODE_A])
        input.x -= 1.f;
    if (key_states[SDL_SCANCODE_LCTRL])
        input.z -= 1.f;
    if (key_states[SDL_SCANCODE_SPACE])
        input.z += 1.f;

    glm::vec3 movement = flatForward * input.y + right * input.x;
    if (glm::length(movement) > 0.0f) {
        movement = glm::normalize(movement);
    }

    camera.x += movement.x * speed * deltaTime;
    camera.y += movement.y * speed * deltaTime;
    camera.z += input.z * speed * deltaTime;
}
