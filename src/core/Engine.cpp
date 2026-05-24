#include "Engine.hpp"

Engine::Engine(Window *window) : window(*window) {};

Engine::~Engine() = default;

void Engine::init() {}

void Engine::run() {
    if (!window.isRunning()) {
        throw std::runtime_error("Failed to run Engine : Window is not running");
    }

    while (window.isRunning()) {
        window.pollEvents();
    }
}

void Engine::cleanup() {}
