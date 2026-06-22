#ifndef DEMO_GAME_HPP
#define DEMO_GAME_HPP

#pragma once
#include "Game.hpp"
#include "RenderObject.hpp"
#include <vector>

class DemoGame final : public Game {
  public:
    void init(Engine &engine) override;
    void update(Engine &engine, float deltaTime) override;

  private:
    struct SpinningObject {
        RenderObjectHandle handle;
        glm::vec3 spinSpeed{0.0f};
    };

    struct OrbitingObject {
        RenderObjectHandle handle;
        glm::vec3 center{0.0f};
        float radius = 0.0f;
        float speed = 0.0f;
        float angle = 0.0f;
    };

    std::vector<SpinningObject> spinningObjects;
    std::vector<OrbitingObject> orbitingObjects;
};

#endif
