cmake_minimum_required(VERSION 3.20)
project(VulkanSDL3Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED)

find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure Vulkan SDK is installed.")
endif()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/vert.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/src/shaders/shader.vert -o ${CMAKE_BINARY_DIR}/shaders/vert.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/shader.vert
    COMMENT "Compiling vertex shader"
)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/frag.spv
    COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/src/shaders/shader.frag -o ${CMAKE_BINARY_DIR}/shaders/frag.spv
    DEPENDS ${CMAKE_SOURCE_DIR}/src/shaders/shader.frag
    COMMENT "Compiling fragment shader"
)

add_custom_target(shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/shaders/vert.spv ${CMAKE_BINARY_DIR}/shaders/frag.spv)

add_executable(${PROJECT_NAME} src/main.cpp)

add_dependencies(${PROJECT_NAME} shaders)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework IOKit" "-framework QuartzCore")
endif()
