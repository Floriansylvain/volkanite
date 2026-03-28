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

set(SOURCES
    src/main.cpp
    src/core/Window.cpp
    src/core/Instance.cpp
    src/core/Device.cpp
    src/core/SwapChain.cpp
    src/core/Pipeline.cpp
)

set(VERT_SHADER ${CMAKE_SOURCE_DIR}/src/shaders/shader.vert)
set(FRAG_SHADER ${CMAKE_SOURCE_DIR}/src/shaders/shader.frag)
set(VERT_SPV ${CMAKE_BINARY_DIR}/shaders/vert.spv)
set(FRAG_SPV ${CMAKE_BINARY_DIR}/shaders/frag.spv)

add_custom_command(
    OUTPUT ${VERT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
    COMMAND ${GLSLC} ${VERT_SHADER} -o ${VERT_SPV}
    DEPENDS ${VERT_SHADER}
    COMMENT "Compiling vertex shader"
)

add_custom_command(
    OUTPUT ${FRAG_SPV}
    COMMAND ${GLSLC} ${FRAG_SHADER} -o ${FRAG_SPV}
    DEPENDS ${FRAG_SHADER}
    COMMENT "Compiling fragment shader"
)

add_custom_target(shaders ALL DEPENDS ${VERT_SPV} ${FRAG_SPV})

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_dependencies(${PROJECT_NAME} shaders)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework IOKit" "-framework QuartzCore")
endif()
