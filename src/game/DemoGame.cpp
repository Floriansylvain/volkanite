#include "DemoGame.hpp"
#include "Engine.hpp"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {
const glm::vec3 SHOWCASE_ORIGIN{15.0f, -5.0f, 10.0f};
} // namespace

void DemoGame::init(Engine &engine) {
    const Engine::FBXModel house = engine.createFBXModel("models/House_scene_01.fbx", ".png");

    engine.placeFBXModel(house, glm::vec3(0.f, 0.f, 0.f));

    // pirate-gold
    // oxidized-metal
    // steelplate1
    const auto selectedTexture = std::string("steelplate1");

    const auto cubeMesh = engine.createCubeMesh(1.f);
    const auto albedo = engine.loadTexture(std::format("textures/{}_albedo.png", selectedTexture));
    const auto normalMap = engine.loadNormalMap(std::format("textures/{}_normal-ogl.png", selectedTexture));
    const auto roughnessMap = engine.loadRoughnessMap(std::format("textures/{}_roughness.png", selectedTexture));
    const auto metallicMap = engine.loadRoughnessMap(std::format("textures/{}_metallic.png", selectedTexture));
    const auto heightMap = engine.loadHeightMap(std::format("textures/{}_height.png", selectedTexture));

    Material cubeMaterial = {};
    cubeMaterial.albedo = albedo;
    cubeMaterial.normalMap = normalMap;
    cubeMaterial.roughnessMap = roughnessMap;
    cubeMaterial.metallicMap = metallicMap;
    cubeMaterial.heightMap = heightMap;

    constexpr int OFFSET = 3;
    constexpr float INNER_RADIUS = 25.0f;
    constexpr float OUTER_RADIUS = 40.0f;
    constexpr float INNER_RADIUS_SQ = INNER_RADIUS * INNER_RADIUS;
    constexpr float OUTER_RADIUS_SQ = OUTER_RADIUS * OUTER_RADIUS;
    constexpr int BOUND = static_cast<int>(OUTER_RADIUS);

    for (int x = -BOUND; x <= BOUND; x += OFFSET) {
        for (int y = -BOUND; y <= BOUND; y += OFFSET) {
            for (int z = 0; z <= BOUND; z += OFFSET) {
                float distSq = float(x * x + y * y + z * z);
                if (distSq < INNER_RADIUS_SQ || distSq > OUTER_RADIUS_SQ)
                    continue;
                RenderObject cube;
                cube.mesh = cubeMesh;
                cube.material = cubeMaterial;
                cube.position = {x, y, z};
                cube.rotation = glm::vec3(glm::sin(float(x)), glm::sin(float(y)), glm::sin(float(z)));
                engine.addRenderObject(std::move(cube));
            }
        }
    }

    {
        const auto bigCubeMesh = engine.createCubeMesh(100.f);
        RenderObject bigCube;
        bigCube.mesh = bigCubeMesh;
        bigCube.material = cubeMaterial;
        bigCube.position = {0.f, 0.f, -150.f};
        bigCube.rotation = glm::vec3({0.f});
        engine.addRenderObject(std::move(bigCube));
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material.albedo = albedo;
        obj.position = SHOWCASE_ORIGIN + glm::vec3(0.f, -6.f, 2.f);
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.push_back({handle, glm::vec3(0.6f, 1.0f, 1.4f)});
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material.albedo = albedo;
        obj.position = SHOWCASE_ORIGIN;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        orbitingObjects.push_back({handle, SHOWCASE_ORIGIN, 6.0f, 1.2f, 0.0f});
    }

    {
        const glm::vec3 center = SHOWCASE_ORIGIN + glm::vec3(0.f, 6.f, -2.f);
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.material.albedo = albedo;
        obj.position = center;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.push_back({handle, glm::vec3(2.0f, -1.5f, 0.8f)});
        orbitingObjects.push_back({handle, center, 4.0f, -0.8f, 0.0f});
    }
}

void DemoGame::update(Engine &engine, const float deltaTime) {
    for (const auto &spinner : spinningObjects) {
        engine.getRenderObject(spinner.handle).rotation += spinner.spinSpeed * deltaTime;
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

    float speed = 10.0f;

    if (key_states[SDL_SCANCODE_LSHIFT])
        speed *= 5;
    if (key_states[SDL_SCANCODE_LALT])
        speed *= 0.25;
    if (key_states[SDL_SCANCODE_W])
        input.y += 1;
    if (key_states[SDL_SCANCODE_S])
        input.y -= 1;
    if (key_states[SDL_SCANCODE_D])
        input.x += 1;
    if (key_states[SDL_SCANCODE_A])
        input.x -= 1;
    if (key_states[SDL_SCANCODE_LCTRL])
        input.z -= 1;
    if (key_states[SDL_SCANCODE_SPACE])
        input.z += 1;

    glm::vec3 movement = flatForward * input.y + right * input.x;
    if (glm::length(movement) > 0.0f) {
        movement = glm::normalize(movement);
    }

    camera.x += movement.x * speed * deltaTime;
    camera.y += movement.y * speed * deltaTime;
    camera.z += input.z * speed * deltaTime;
}
