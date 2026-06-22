#ifndef DEMO_GAME_HPP
#define DEMO_GAME_HPP

#pragma once
#include "Game.hpp"

class DemoGame final : public Game {
  public:
    void init(Engine &engine) override;
    void update(Engine &engine, float deltaTime) override;
};

#endif
