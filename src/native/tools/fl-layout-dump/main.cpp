// Emits the native shared-memory struct layout as JSON.
//
// FrameLedger.Infrastructure.Tests consumes this (ShmLayoutMirrorTests) and
// asserts that the C# [StructLayout(LayoutKind.Sequential)] mirrors in
// FrameLedger.Shared match field for field. CLAUDE.md §Struct mirroring requires
// a test that checks sizeof AND every field offset on both sides; this is the
// native half, and as of 2026-08-05 the managed half exists -- this comment was
// present tense about a consumer that did not, which is what
// 20_OPEN_QUESTIONS §R10 recorded.
//
// EMIT EVERY FIELD, including the reserved tails. The test walks this list in
// both directions: a field here and not in C# fails, and a field in C# and not
// here fails too. A dump that quietly stops reporting a field would otherwise
// shrink what the mirror checks while every assertion still passed.
//
// Struct drift between C++ and C# is the most dangerous silent bug in this
// architecture: nothing crashes, the Agent just reads garbage into fields that
// look plausible.

#include <cstddef>
#include <cstdio>
#include <fl_shm.h>

using namespace fl;

namespace {

void Field(const char* name, size_t offset, size_t size, bool last = false) {
    std::printf("      { \"name\": \"%s\", \"offset\": %zu, \"size\": %zu }%s\n", name, offset, size, last ? "" : ",");
}

}    // namespace

int main() {
    std::printf("{\n");
    std::printf("  \"layoutVersion\": %u,\n", FL_SHM_LAYOUT_VERSION);
    std::printf("  \"regions\": { \"handshake\": %u, \"writer\": %u, \"control\": %u, \"ring\": %u },\n",
                FL_SHM_HANDSHAKE_OFFSET, FL_SHM_WRITER_OFFSET, FL_SHM_CONTROL_OFFSET, FL_SHM_RING_OFFSET);
    std::printf("  \"structs\": {\n");

    std::printf("    \"FlShmHandshake\": { \"size\": %zu, \"fields\": [\n", sizeof(FlShmHandshake));
    Field("layoutVersion", offsetof(FlShmHandshake, layoutVersion), sizeof(uint32_t));
    Field("recordSize", offsetof(FlShmHandshake, recordSize), sizeof(uint32_t));
    Field("capacity", offsetof(FlShmHandshake, capacity), sizeof(uint32_t));
    Field("pid", offsetof(FlShmHandshake, pid), sizeof(uint32_t));
    Field("buildId", offsetof(FlShmHandshake, buildId), 32);
    Field("qpcEpoch", offsetof(FlShmHandshake, qpcEpoch), sizeof(uint64_t));
    Field("adapterLuid", offsetof(FlShmHandshake, adapterLuid), sizeof(uint64_t), true);
    std::printf("    ] },\n");

    std::printf("    \"FlWriterState\": { \"size\": %zu, \"fields\": [\n", sizeof(FlWriterState));
    Field("writeIndex", offsetof(FlWriterState, writeIndex), sizeof(uint64_t));
    Field("status", offsetof(FlWriterState, status), sizeof(uint32_t));
    Field("apiMask", offsetof(FlWriterState, apiMask), sizeof(uint32_t));
    Field("faultCount", offsetof(FlWriterState, faultCount), sizeof(uint32_t));
    Field("vramBudgetMb", offsetof(FlWriterState, vramBudgetMb), sizeof(uint32_t));
    // The reserved tail is emitted too. It is part of the layout -- "room for
    // additive fields" is a promise about WHERE they may go -- and a mirror that
    // declares it while the dump stays silent cannot be checked in both
    // directions. Found by the managed test's reverse walk on 2026-08-05.
    Field("rtTier", offsetof(FlWriterState, rtTier), sizeof(uint32_t));
    Field("hooksInstalledMask", offsetof(FlWriterState, hooksInstalledMask), sizeof(uint32_t));
    Field("rtStateObjectsCreated", offsetof(FlWriterState, rtStateObjectsCreated), sizeof(uint32_t));
    Field("rasterPsoCreated", offsetof(FlWriterState, rasterPsoCreated), sizeof(uint32_t));
    Field("runtimeCensus", offsetof(FlWriterState, runtimeCensus), sizeof(uint32_t));
    Field("slTagCensus", offsetof(FlWriterState, slTagCensus), sizeof(uint32_t));
    Field("dxgiPresentsUnseen", offsetof(FlWriterState, dxgiPresentsUnseen), sizeof(uint32_t));
    Field("dxgiPresentSamples", offsetof(FlWriterState, dxgiPresentSamples), sizeof(uint32_t));
    Field("loaderSignals", offsetof(FlWriterState, loaderSignals), sizeof(uint16_t));
    Field("earlyStopFamily", offsetof(FlWriterState, earlyStopFamily), sizeof(uint16_t));
    Field("dxgiPresentsBeforeHook", offsetof(FlWriterState, dxgiPresentsBeforeHook), sizeof(uint32_t), true);
    std::printf("    ] },\n");

    std::printf("    \"FlControlBlock\": { \"size\": %zu, \"fields\": [\n", sizeof(FlControlBlock));
    Field("pauseRequested", offsetof(FlControlBlock, pauseRequested), sizeof(uint32_t));
    Field("unhookRequested", offsetof(FlControlBlock, unhookRequested), sizeof(uint32_t));
    Field("overlayEnabled", offsetof(FlControlBlock, overlayEnabled), sizeof(uint32_t));
    Field("guardTicks", offsetof(FlControlBlock, guardTicks), sizeof(uint32_t));
    Field("logFlushRequested", offsetof(FlControlBlock, logFlushRequested), sizeof(uint32_t));
    Field("reserved", offsetof(FlControlBlock, reserved), sizeof(uint32_t) * 11, true);
    std::printf("    ] },\n");

    std::printf("    \"FlFrameRecord\": { \"size\": %zu, \"fields\": [\n", sizeof(FlFrameRecord));
    Field("qpc", offsetof(FlFrameRecord, qpc), sizeof(uint64_t));
    Field("frameIndex", offsetof(FlFrameRecord, frameIndex), sizeof(uint32_t));
    Field("presentFlags", offsetof(FlFrameRecord, presentFlags), sizeof(uint32_t));
    Field("syncInterval", offsetof(FlFrameRecord, syncInterval), sizeof(uint16_t));
    Field("renderW", offsetof(FlFrameRecord, renderW), sizeof(uint16_t));
    Field("renderH", offsetof(FlFrameRecord, renderH), sizeof(uint16_t));
    Field("outputW", offsetof(FlFrameRecord, outputW), sizeof(uint16_t));
    Field("outputH", offsetof(FlFrameRecord, outputH), sizeof(uint16_t));
    Field("api", offsetof(FlFrameRecord, api), sizeof(uint8_t));
    Field("upscaler", offsetof(FlFrameRecord, upscaler), sizeof(uint8_t));
    Field("upscalerQuality", offsetof(FlFrameRecord, upscalerQuality), sizeof(uint8_t));
    Field("fgMode", offsetof(FlFrameRecord, fgMode), sizeof(uint8_t));
    Field("rtFlags", offsetof(FlFrameRecord, rtFlags), sizeof(uint8_t));
    Field("colorSpace", offsetof(FlFrameRecord, colorSpace), sizeof(uint8_t));
    Field("dispatchRaysVolume", offsetof(FlFrameRecord, dispatchRaysVolume), sizeof(uint32_t));
    Field("psoCreatedThisFrame", offsetof(FlFrameRecord, psoCreatedThisFrame), sizeof(uint16_t));
    Field("maxTraceRecursionDepth", offsetof(FlFrameRecord, maxTraceRecursionDepth), sizeof(uint8_t));
    Field("featureFlags", offsetof(FlFrameRecord, featureFlags), sizeof(uint8_t));
    Field("measuredMask", offsetof(FlFrameRecord, measuredMask), sizeof(uint16_t));
    Field("upscalerSharpness", offsetof(FlFrameRecord, upscalerSharpness), sizeof(uint8_t));
    Field("fgEvaluations", offsetof(FlFrameRecord, fgEvaluations), sizeof(uint8_t));
    Field("vramUsedMb", offsetof(FlFrameRecord, vramUsedMb), sizeof(uint32_t));
    Field("reflexLatencyUs", offsetof(FlFrameRecord, reflexLatencyUs), sizeof(uint32_t));
    Field("dxgiUnseen", offsetof(FlFrameRecord, dxgiUnseen), sizeof(uint8_t));
    Field("reserved", offsetof(FlFrameRecord, reserved), sizeof(uint8_t) * 3);
    Field("seq", offsetof(FlFrameRecord, seq), sizeof(uint32_t));
    Field("swapchainId", offsetof(FlFrameRecord, swapchainId), sizeof(uint32_t), true);
    std::printf("    ] }\n");

    std::printf("  }\n}\n");
    return 0;
}
