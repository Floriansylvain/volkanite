cmake_minimum_required(VERSION 3.20)
project(volkanite LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(glm CONFIG REQUIRED)
find_program(SLANGC slangc REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")

set(SLANG_SHADER_NAMES shader text cull/DepthToMip0 cull/Downsample cull/CullInstances)
set(SLANG_SPV_FILES "")

foreach (SHADER_NAME ${SLANG_SHADER_NAMES})
    set(SLANG_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${SHADER_NAME}.slang")
    
    set(SLANG_SPV_OUTPUT "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/${SHADER_NAME}.spv")
    set(SLANG_SPV_BIN "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT "${SLANG_SPV_BIN}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders/cull"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/cull"
        COMMAND ${SLANGC} "${SLANG_SHADER}"
            -target spirv
            -profile spirv_1_4
            -O0
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -o "${SLANG_SPV_BIN}"
        COMMAND ${CMAKE_COMMAND} -E copy "${SLANG_SPV_BIN}" "${SLANG_SPV_OUTPUT}"
        DEPENDS "${SLANG_SHADER}"
        COMMENT "Compiling shader stage: ${SHADER_NAME}.slang"
        VERBATIM
    )
    list(APPEND SLANG_SPV_FILES "${SLANG_SPV_BIN}")
endforeach ()

add_custom_target(generate_shaders DEPENDS ${SLANG_SPV_FILES})

add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_dependencies(${PROJECT_NAME} generate_shaders)

add_library(ufbx STATIC external/ufbx/ufbx.c)
target_include_directories(ufbx PUBLIC external/ufbx)

if(MSVC)
    target_compile_options(ufbx PRIVATE /W0)
else()
    target_compile_options(ufbx PRIVATE -w)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/models"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/models"
    COMMENT "Syncing models folder..."
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/textures"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/textures"
    COMMENT "Syncing textures folder..."
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-shared
    SDL3_image::SDL3_image-shared
    glm::glm
    ufbx
)

target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core/pch.hpp")

if (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework IOKit" "-framework QuartzCore")
endif ()
