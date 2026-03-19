cmake_minimum_required(VERSION 3.20)
project(VulkanSDL3Project LANGUAGES CXX)

# Standard C++20 (recommandé pour la prog graphique moderne)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Recherche de Vulkan
find_package(Vulkan REQUIRED)

# 2. Recherche de SDL3
find_package(SDL3 REQUIRED)

# 3. Création de l'exécutable
add_executable(${PROJECT_NAME} src/main.cpp)

# 4. Liens avec les bibliothèques
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
)

# Spécificité macOS : On doit lier les frameworks système nécessaires à MoltenVK
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework IOKit" "-framework QuartzCore")
endif()
