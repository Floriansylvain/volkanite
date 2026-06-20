# volkanite

This is my playground to learn:

1. c++
2. graphics programming
3. vulkan api
4. linear algebra

## requirements

### build

- CMake 3.20
- clang

### dependencies & sdks

- Vulkan SDK (1.4.341)
- SDL3 (3.4.8)
- glm (1.0.3)

## Installing dependencies

### macOS

```bash
brew install cmake sdl3 sdl3_image glm
```

Download and run the Vulkan SDK installer from [vulkan.lunarg.com](https://vulkan.lunarg.com)

### Windows (Visual Studio)

1. install Visual Studio 2026+ with the "Desktop development with C++" workload
2. install the [Vulkan SDK](https://vulkan.lunarg.com)
3. set a `VCPKG_ROOT` environment variable pointing at a vcpkg checkout (clone [microsoft/vcpkg](https://github.com/microsoft/vcpkg) anywhere and run `bootstrap-vcpkg.bat` once)

## Build

```bash
cmake -B build
cmake --build build
```
