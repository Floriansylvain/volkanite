#include "DemoGame.hpp"
#include "Engine.hpp"
#include "TerrainTypes.hpp"
#include <SDL3/SDL.h>

namespace {
constexpr glm::vec3 SHOWCASE_ORIGIN{15.0f, -5.0f, 10.0f};
} // namespace

void DemoGame::init(Engine &engine) {
    const Engine::FBXModel house = engine.createFBXModel("models/House_scene_01.fbx", ".png");

    engine.placeFBXModel(house, glm::vec3(0.f, 0.f, 0.f));

    // const auto selectedTexture = std::string("pirate-gold");
    // const auto selectedTexture = std::string("oxidized-metal-clad");
    // const auto selectedTexture = std::string("steelplate1");
    const auto selectedTexture = std::string("wet-stones-with-sand1");
    // const auto selectedTexture = std::string("grassy-meadow1");
    //  const auto selectedTexture = std::string("dented-metal");
    //  const auto selectedTexture = std::string("base-white-tile");
    //  const auto selectedTexture = std::string("grass1"); // no metallic map!

    auto cubeMesh = engine.createCubeMesh(1.f);
    cubeMesh->uvScale = glm::vec2(1.f);
    const auto albedo = engine.loadTexture(std::format("textures/{}_albedo.png", selectedTexture));
    const auto normalMap = engine.loadNormalMap(std::format("textures/{}_normal-ogl.png", selectedTexture));
    const auto ormMap = engine.loadOrmMap(std::format("textures/{}_roughness.png", selectedTexture),
                                          std::format("textures/{}_metallic.png", selectedTexture),
                                          std::format("textures/{}_height.png", selectedTexture));
    // const auto ormMap = engine.loadOrmMap(std::format("textures/{}_roughness.png", selectedTexture), "",
    //                                       std::format("textures/{}_height.png", selectedTexture));
    //  const auto ormMap = engine.loadOrmMapFile("textures/oxidized-metal-clad_orm.dds");

    Material cubeMaterial = {};
    cubeMaterial.albedo = albedo;
    cubeMaterial.normalMap = normalMap;
    cubeMaterial.ormMap = ormMap;

    {
        TerrainConfig terrainConfig;
        terrainConfig.origin = glm::vec2(0.0f, 0.0f);
        terrainConfig.rootSize = 16384.0f;
        terrainConfig.maxDepth = 6;
        terrainConfig.chunkResolution = 33;
        terrainConfig.splitFactor = 2.0f;
        terrainConfig.textureWorldScale = 16.0f;

        terrainConfig.uvScale = glm::vec2(16.0f);

        terrainConfig.noise.scale = 150.0f;
        terrainConfig.noise.heightScale = 100.0f;
        terrainConfig.noise.baseHeight = -100.0f;
        terrainConfig.noise.octaves = 4;
        terrainConfig.noise.persistence = 0.5f;
        terrainConfig.noise.lacunarity = 2.0f;

        terrainConfig.noise.regionThreshold = 0.55f;
        terrainConfig.noise.regionBlendWidth = 0.2f;
        terrainConfig.noise.regionScale = 1000.0f;
        terrainConfig.noise.ridgeSharpness = 0.01f;
        terrainConfig.noise.heightRedistribution = 1.00f;
        terrainConfig.fineChunkResolution = 100;

        terrainConfig.material = cubeMaterial;
        terrainConfig.morphRatio = 0.1f;

        engine.createTerrain(terrainConfig);
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = cubeMaterial;
        obj.position = SHOWCASE_ORIGIN + glm::vec3(0.f, -6.f, 2.f);
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.emplace_back(handle, glm::vec3(0.6f, 1.0f, 1.4f));
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = cubeMaterial;
        obj.position = SHOWCASE_ORIGIN;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        orbitingObjects.emplace_back(handle, SHOWCASE_ORIGIN, 6.0f, 1.2f, 0.0f);
    }

    {
        const glm::vec3 center = SHOWCASE_ORIGIN + glm::vec3(0.f, 6.f, -2.f);
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material = cubeMaterial;
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

    constexpr float maxPitch = 89.0f;
    camera.pitch = std::clamp(camera.pitch, -maxPitch, maxPitch);

    const float yawRad = glm::radians(camera.yaw);
    const glm::vec3 flatForward(cos(yawRad), sin(yawRad), 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f, 0.0f, 1.0f)));

    const bool *key_states = SDL_GetKeyboardState(nullptr);
    glm::vec3 input = {0.f, 0.f, 0.f};

    float speed = 500.0f;

    if (key_states[SDL_SCANCODE_LSHIFT])
        speed *= 5.f;
    if (key_states[SDL_SCANCODE_LALT])
        speed *= 0.25f;
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
