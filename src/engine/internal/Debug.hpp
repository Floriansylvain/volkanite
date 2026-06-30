#ifndef DEBUG_HPP
#define DEBUG_HPP

#pragma once
#include <array>

enum class GpuPass : uint32_t { FrameSetup, HiZBuild, Culling, OpaqueGeometry, Xray, TextOverlay, Total, Count };
enum class CpuPass : uint32_t {
    PollEvents,
    CameraUpdate,
    FenceWait,
    AcquireImage,
    BufferUpdate,
    RecordCmd,
    Submit,
    Present,
    Total,
    Count
};

struct DebugFrameInfo {
    int width = 0;
    int height = 0;
    uint32_t msaaSamples = 1;
    float maxAnisotropy = 0.0f;
    bool vsyncOn = false;
    std::string presentModeName;

    uint32_t drawCallCount = 0;
    uint64_t vertexCount = 0;

    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraZ = 0.0f;
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;

    bool wireframe = false;
    bool occlusionCulling = false;
    bool xray = false;
    bool perfOverlayVisible = false;
};

class Debug {
  public:
    void addCpuTime(CpuPass pass, std::chrono::high_resolution_clock::time_point start);
    void recordGpuPass(GpuPass pass, float ms);
    void endGpuSample();

    void update(std::chrono::time_point<std::chrono::steady_clock> frameStart, const DebugFrameInfo &info);

    const std::vector<std::string> &lines() const { return debugLines; }
    const std::vector<std::string> &perfLines() const { return perfDebugLines; }

  private:
    void rebuildDebugLines(float avgFrameTimeMs, const DebugFrameInfo &info);
    void rebuildPerfDebugLines(float avgFrameTimeMs);

    constexpr static float DEBUG_REBUILD_INTERVAL_MS = 100.0f;

    float frameTimeAccumulator = 0.0f;
    uint32_t frameCountAccumulator = 0;

    std::array<float, static_cast<size_t>(CpuPass::Count)> cpuTimeAccumulatorMs{};
    std::array<float, static_cast<size_t>(GpuPass::Count)> gpuTimeAccumulatorMs{};
    uint32_t gpuTimingSampleCount = 0;

    std::vector<std::string> debugLines;
    std::vector<std::string> perfDebugLines;
};

#endif
