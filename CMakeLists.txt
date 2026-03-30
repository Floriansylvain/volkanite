cmake_minimum_required(VERSION 3.20)
project(VulkanSDL3Project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED)
find_package(glm CONFIG REQUIRED)

find_program(SLANGC slangc REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")

set(SLANG_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/shader.slang")
set(SLANG_SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/shader.spv")

add_custom_command(
    OUTPUT "${SLANG_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND ${SLANGC} "${SLANG_SHADER}" 
            -target spirv 
            -profile spirv_1_4 
            -emit-spirv-directly 
            -fvk-use-entrypoint-name 
            -o "${SLANG_SPV}"
    DEPENDS "${SLANG_SHADER}"
    VERBATIM
)

add_custom_target(generate_shaders DEPENDS "${SLANG_SPV}")

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_dependencies(${PROJECT_NAME} generate_shaders)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-shared
    glm::glm
)

if(APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE "-framework" "Cocoa" "-framework" "IOKit" "-framework" "CoreVideo")
endif()
