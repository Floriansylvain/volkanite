#include "engine/Engine.hpp"

#include <engine/internal/Window.hpp>
#include <game/DemoGame.hpp>
#include <iostream>
#include <toml.hpp>

int main() {
    Window window;
    VulkanContext vkCtx{};
    Engine engine(&window, &vkCtx);
    DemoGame game;

    toml::table config;
    try {
        config = toml::parse_file("config.toml");
    } catch (const toml::parse_error &err) {
        throw std::runtime_error(std::format("config.toml parsing failed: {}\n", err.description()));
    }

    auto windowConf = config["window"].as_table();
    if (!windowConf) {
        throw std::runtime_error("Missing [window] section in config.toml.");
    }

    int width = windowConf->get("width")->value_or(1920);
    int height = windowConf->get("height")->value_or(1080);
    bool maximised = windowConf->get("maximised")->value_or(false);
    bool resizable = windowConf->get("resizable")->value_or(true);

    try {
        window.init("volkanite", width, height, maximised, resizable);

        engine.setGame(&game);
        engine.init();
        engine.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
