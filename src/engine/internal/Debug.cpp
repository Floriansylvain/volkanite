#include "Debug.hpp"

#include <format>
#include <span>

namespace {

constexpr std::array<const char *, static_cast<size_t>(CpuPass::Count)> cpuPassNames = {
    "poll", "camera update", "fence wait", "acquire", "buffer update", "record", "submit", "present", "CPU total"};

constexpr std::array<const char *, static_cast<size_t>(GpuPass::Count)> gpuPassNames = {
    "frame setup", "HiZ build", "culling", "opaque", "xray", "text", "GPU total"};

template <typename Names>
float appendTrackedPassLines(std::vector<std::string> &out, const Names &names, std::span<const float> accumMs, float divisor) {
    float trackedSum = 0.0f;
    for (size_t i = 0; i < names.size() - 1; ++i) {
        const float avgMs = accumMs[i] / divisor;
        trackedSum += avgMs;
        out.push_back(std::format("{}: {:.3f}ms", names[i], avgMs));
    }
    return trackedSum;
}

} // namespace

void Debug::addCpuTime(const CpuPass pass, const std::chrono::high_resolution_clock::time_point start) {
    const float ms = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
    cpuTimeAccumulatorMs[static_cast<size_t>(pass)] += ms;
}

void Debug::recordGpuPass(const GpuPass pass, const float ms) { gpuTimeAccumulatorMs[static_cast<size_t>(pass)] += ms; }

void Debug::endGpuSample() { gpuTimingSampleCount++; }

void Debug::update(const std::chrono::time_point<std::chrono::steady_clock> frameStart, const DebugFrameInfo &info) {
    const float frameTimeMs =
        std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - frameStart).count();

    frameTimeAccumulator += frameTimeMs;
    frameCountAccumulator++;

    if (frameTimeAccumulator < DEBUG_REBUILD_INTERVAL_MS)
        return;

    const float avgFrameTimeMs = frameTimeAccumulator / static_cast<float>(frameCountAccumulator);

    rebuildDebugLines(avgFrameTimeMs, info);
    rebuildPerfDebugLines(avgFrameTimeMs);

    frameTimeAccumulator = 0.0f;
    frameCountAccumulator = 0;
}

void Debug::rebuildDebugLines(const float avgFrameTimeMs, const DebugFrameInfo &info) {
    debugLines.clear();

    debugLines.push_back(std::format("MSAA: {}x", info.msaaSamples));
    debugLines.push_back(std::format("ANISOTROPY: {:.0f}x", info.maxAnisotropy));
    debugLines.push_back(std::format("V-SYNC: {} ({})", info.vsyncOn ? "ON" : "OFF", info.presentModeName));
    debugLines.emplace_back("");

    const float avgCpuWaitMs =
        cpuTimeAccumulatorMs[static_cast<size_t>(CpuPass::FenceWait)] / static_cast<float>(frameCountAccumulator);
    const float avgGpuBusyMs = gpuTimingSampleCount > 0 ? gpuTimeAccumulatorMs[static_cast<size_t>(GpuPass::Total)] /
                                                              static_cast<float>(gpuTimingSampleCount)
                                                        : 0.0f;
    const float avgGpuWaitMs = avgFrameTimeMs > avgGpuBusyMs ? avgFrameTimeMs - avgGpuBusyMs : 0.0f;

    debugLines.push_back(std::format("frametime: {:.2f}ms ({:.0f} fps)", avgFrameTimeMs, 1000.0f / avgFrameTimeMs));
    debugLines.push_back(std::format("CPU Wait: {:.2f}ms", avgCpuWaitMs));
    debugLines.push_back(std::format("GPU Wait: {:.2f}ms", avgGpuWaitMs));
    debugLines.push_back(std::format("bottleneck: {}", avgCpuWaitMs > avgGpuWaitMs ? "GPU" : "CPU"));
    debugLines.push_back(std::format("draws: {}", info.drawCallCount));
    debugLines.push_back(std::format("verts: {}", info.vertexCount));
    debugLines.emplace_back("");

    debugLines.emplace_back("CAMERA");
    debugLines.push_back(std::format("x {:.2f}", info.cameraX));
    debugLines.push_back(std::format("y {:.2f}", info.cameraY));
    debugLines.push_back(std::format("z {:.2f}", info.cameraZ));
    debugLines.push_back(std::format("yaw {:.2f}", info.cameraYaw));
    debugLines.push_back(std::format("pitch {:.2f}", info.cameraPitch));
    debugLines.emplace_back("");

    debugLines.emplace_back("KEYBINDS");
    debugLines.emplace_back("Directions: W A S D");
    debugLines.emplace_back("Height: SPACE LCTRL");
    debugLines.emplace_back("Speed: LSHIFT LALT");
    debugLines.push_back(std::format("T: wireframe ({})", info.wireframe ? "ON" : "OFF"));
    debugLines.push_back(std::format("C: occlusion culling ({})", info.occlusionCulling ? "ON" : "OFF"));
    debugLines.push_back(std::format("X: x-ray ({})", info.xray ? "ON" : "OFF"));
    debugLines.push_back(std::format("P: perf overlay ({})", info.perfOverlayVisible ? "ON" : "OFF"));
}

void Debug::rebuildPerfDebugLines(const float avgFrameTimeMs) {
    perfDebugLines.clear();

    perfDebugLines.emplace_back("CPU PASSES");
    const float cpuTrackedSum = appendTrackedPassLines(
        perfDebugLines, cpuPassNames, std::span(cpuTimeAccumulatorMs.data(), static_cast<size_t>(CpuPass::Total)),
        static_cast<float>(frameCountAccumulator));
    perfDebugLines.push_back(std::format("Unaccounted: {:.3f}ms", avgFrameTimeMs - cpuTrackedSum));
    perfDebugLines.push_back(std::format("{}: {:.3f}ms", cpuPassNames.back(), cpuTrackedSum));
    perfDebugLines.emplace_back("");
    cpuTimeAccumulatorMs.fill(0.0f);

    if (gpuTimingSampleCount > 0) {
        perfDebugLines.emplace_back("GPU PASSES");
        const auto divisor = static_cast<float>(gpuTimingSampleCount);
        const float gpuTrackedSum = appendTrackedPassLines(
            perfDebugLines, gpuPassNames, std::span(gpuTimeAccumulatorMs.data(), static_cast<size_t>(GpuPass::Total)), divisor);
        const float avgTotalMs = gpuTimeAccumulatorMs[static_cast<size_t>(GpuPass::Total)] / divisor;
        perfDebugLines.push_back(std::format("Unaccounted: {:.3f}ms", avgTotalMs - gpuTrackedSum));
        perfDebugLines.push_back(std::format("{}: {:.3f}ms", gpuPassNames.back(), avgTotalMs));
        perfDebugLines.emplace_back("");
    }
    gpuTimeAccumulatorMs.fill(0.0f);
    gpuTimingSampleCount = 0;
}
