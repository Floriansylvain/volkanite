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

    const auto cubeMesh = engine.createCubeMesh(1.f);
    const auto texture = engine.loadTexture("textures/bricks.jpg");

    constexpr int OFFSET = 3;
    constexpr float INNER_RADIUS = 25.0f;
    constexpr float OUTER_RADIUS = 40.0f;
    constexpr float INNER_RADIUS_SQ = INNER_RADIUS * INNER_RADIUS;
    constexpr float OUTER_RADIUS_SQ = OUTER_RADIUS * OUTER_RADIUS;
    constexpr int BOUND = static_cast<int>(OUTER_RADIUS);

    for (int x = -BOUND; x <= BOUND; x += OFFSET) {
        for (int y = -BOUND; y <= BOUND; y += OFFSET) {
            for (int z = -BOUND; z <= BOUND; z += OFFSET) {
                float distSq = static_cast<float>(x * x + y * y + z * z);
                if (distSq >= INNER_RADIUS_SQ && distSq <= OUTER_RADIUS_SQ) {
                    RenderObject cube;
                    cube.mesh = cubeMesh;
                    cube.texture = texture;
                    cube.position = {x, y, z};
                    cube.rotation = glm::vec3(glm::sin(static_cast<float>(x)), glm::sin(static_cast<float>(y)),
                                              glm::sin(static_cast<float>(z)));
                    engine.addRenderObject(std::move(cube));
                }
            }
        }
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.texture = texture;
        obj.position = SHOWCASE_ORIGIN + glm::vec3(0.f, -6.f, 2.f);
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        spinningObjects.push_back({handle, glm::vec3(0.6f, 1.0f, 1.4f)});
    }

    {
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.texture = texture;
        obj.position = SHOWCASE_ORIGIN;
        const RenderObjectHandle handle = engine.addRenderObject(std::move(obj));
        orbitingObjects.push_back({handle, SHOWCASE_ORIGIN, 6.0f, 1.2f, 0.0f});
    }

    {
        const glm::vec3 center = SHOWCASE_ORIGIN + glm::vec3(0.f, 6.f, -2.f);
        RenderObject obj;
        obj.mesh = cubeMesh;
        obj.texture = texture;
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

    constexpr float speed = 10.0f;
    camera.x += movement.x * speed * deltaTime;
    camera.y += movement.y * speed * deltaTime;
    camera.z += input.z * speed * deltaTime;
}
