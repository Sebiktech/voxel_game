# Voxel Game (C++/Vulkan + CMake) — Starter

This is a tiny starter to bootstrap a voxel game in C++ with Vulkan and CMake.

## Features
- CMake with FetchContent (GLFW, glm)
- Vulkan instance/device creation, validation layers, GLFW window/surface
- Basic project structure for world (`Chunk`, `Mesher` stubs)
- Shader pipeline placeholders (GLSL) with optional `glslc` build step

## Prereqs
- **Vulkan SDK** installed and on PATH (https://vulkan.lunarg.com/)
- CMake >= 3.20
- A C++20 compiler
- Git (for FetchContent)

## Build
```bash
# Windows (MSVC) or Linux
cmake -S . -B build
cmake --build build --config Debug
./build/voxel_game   # or .\build\Debug\voxel_game.exe on Windows
```

> Tip: If `glslc` isn't found, shaders won't compile automatically. You can compile them manually or ensure the Vulkan SDK's `Bin/` is on PATH.

## Next steps
- Implement a **swapchain** and a simple render pass
- Fill in `meshChunk` with a naive face mesher, then upgrade to **greedy meshing**
- Add **camera**, ** MVP **, and upload vertex/index buffers
- Add **chunk streaming**, world gen (Simplex/Perlin), frustum culling
- Introduce **texture atlas** + **block palette**
- Use **Vulkan Memory Allocator (VMA)** and **meshoptimizer** later
