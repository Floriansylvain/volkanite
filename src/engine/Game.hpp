#ifndef GAME_HPP
#define GAME_HPP

class Engine;

class Game {
  public:
    virtual ~Game() = default;

    virtual void init(Engine &engine) = 0;
    virtual void update(Engine &engine, float deltaTime) = 0;
};

#endif
