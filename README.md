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

Download and run the Vulkan SDK installer from [vulkan.lunarg.com](https://vulkan.lunarg.com) (includes MoltenVK and
`slangc`).

### Windows

1. install [CMake](https://cmake.org/download/) and Visual Studio with the "Desktop development with C++" workload.
2. install the [Vulkan SDK](https://vulkan.lunarg.com) (includes `slangc`).
3. install the remaining libraries with [vcpkg](https://github.com/microsoft/vcpkg):

   ```powershell
   vcpkg install sdl3 sdl3-image glm
   ```

4. configure the project with the vcpkg toolchain:

   ```powershell
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
   ```

### Linux (Debian/Ubuntu based)

```bash
sudo apt install cmake clang libglm-dev
```

Install the Vulkan SDK by following
the [Linux setup guide](https://vulkan.lunarg.com/doc/sdk/latest/linux/getting_started.html) (includes `slangc`).

SDL3 and SDL3_image aren't yet packaged on Debian/Ubuntu, so it's good luck

## Build

```bash
cmake -B build
cmake --build build
```
