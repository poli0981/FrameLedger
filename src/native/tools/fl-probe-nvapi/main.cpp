// fl-probe-nvapi — proves the vendored NVAPI material is usable, and that L3
// degrades cleanly when it is not.
//
// WHY THIS EXISTS AT ALL. Vendoring added ~2.4 MB of headers and an import
// library that NOTHING compiles against: docs/18_GPU_VENDOR_APIS.md §L3's
// NvapiTelemetrySource is the next piece of work, not this one. A vendored
// dependency with no consumer is a dependency whose header closure could be
// incomplete, whose .lib could be the wrong architecture, and whose
// load-behaviour assumption could be wrong — with every gate green. This probe
// is what makes "vendored" mean "verified".
//
// It answers three things, and the third is the one that matters for CI:
//
//   1. THE HEADER CLOSURE IS COMPLETE. Nine headers were taken out of a
//      nineteen-file repository. If one include is missing this does not
//      compile, which is the entire point of compiling it.
//   2. amd64/nvapi64.lib LINKS x64. A 32-bit .lib fails at link, loudly.
//   3. NvAPI_Initialize FAILING IS A NORMAL CONDITION, not a crash and not a
//      non-zero exit. §L3 requires exactly that: "nvapi64.dll may be absent (no
//      NVIDIA GPU) or older than our headers ... must disable L3 cleanly, not
//      throw." A CI runner has no NVIDIA GPU, so CI exercises the degradation
//      path and the dev box exercises the success path. Both are green, and the
//      probe SAYS WHICH ONE IT TOOK rather than printing the same line either
//      way — an observation that reads identically in both cases carries no
//      information (docs/20_OPEN_QUESTIONS.md §S29).
//
// WHAT IT IS NOT: a telemetry source. It reads driver version and GPU name and
// stops. Thermals, clocks, utilisation, throttle reasons and Reflex latency all
// belong to NvapiTelemetrySource, with the capability matrix restructured under
// them first (docs/18_GPU_VENDOR_APIS.md §Capability matrix).

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nvapi.h"

namespace {

bool g_failed = false;

void Check(bool condition, const char* what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) {
        g_failed = true;
    }
}

// --- --ngx-state <pid>: what the DRIVER says about a process's DLSS features -------
//
// WHY THIS MODE EXISTS (2026-09-05, 20_OPEN_QUESTIONS §H5 / §H11). Four real titles
// run DLSS through NGX directly and drive DLSS Frame Generation on a path the
// Streamline hooks never see (Lies of P, Hell Is Us, Expedition 33, Black Myth:
// Wukong), and a fifth (Dying Light: The Beast, Streamline 2.8) presents its
// generated frames where the present hook cannot count them. The NGX SDK is
// licence-blocked, so nothing in the Overlay may hook nvngx_*.dll. NVAPI is MIT and
// vendored, and NvAPI_NGX_GetNGXOverrideState (R570+) is documented as returning,
// FOR A GIVEN PROCESS ID, the driver's own feedback bits for Super Resolution, Ray
// Reconstruction and Frame Generation -- DLL loaded, feature created, evaluated, FG
// mode, multi-frame -- plus the scaling ratio, the performance (quality) mode and the
// frame-generation count. Out of process, by PID, from the driver's bookkeeping:
// no game memory, no hook, no vendor binary patched.
//
// WHAT IS NOT KNOWN, and why this is a probe rather than a producer: the API's name
// says "override", and whether its feedback bits are populated for a process with
// no NVIDIA-app override configured is unmeasured. Whether "frameGenerationCount" is
// the title's multiplier or only an override target is unmeasured. This mode prints
// the raw words and every decoded bit so the owner's run against a running title
// answers both, before any report line is built on it.
struct FlagName {
    NvU64       bit;
    const char* name;
};

constexpr FlagName kFlags[] = {
    {NV_NGX_DLSS_OVERRIDE_FLAG_INITIALIZED, "INITIALIZED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ENABLED, "ENABLED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_DLL_EXISTS, "DLL_EXISTS"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_DLL_LOADED, "DLL_LOADED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_DLL_SELECTED, "DLL_SELECTED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_PRESET, "PRESET"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_PERF_MODE, "PERF_MODE"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_SCALING_RATIO, "SCALING_RATIO"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_OPTIMAL_SETTINGS, "OPTIMAL_SETTINGS"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_CREATED, "CREATED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_EVALUATE, "EVALUATE"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_FG_MODE, "FG_MODE"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_SR_DLAA_MODE, "SR_DLAA_MODE"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_FG_MULTI_FRAME, "FG_MULTI_FRAME"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ERR_FAILED, "ERR_FAILED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ERR_DENIED, "ERR_DENIED"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ERR_DRS, "ERR_DRS"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ERR_NOT_FOUND, "ERR_NOT_FOUND"},
    {NV_NGX_DLSS_OVERRIDE_FLAG_ERR_DLL_LOAD, "ERR_DLL_LOAD"},
};

void PrintMask(const char* feature, NvU64 mask) {
    std::printf("  %-4s feedback 0x%016llX :", feature, static_cast<unsigned long long>(mask));
    bool any = false;
    for (const FlagName& f : kFlags) {
        if ((mask & f.bit) != 0u) {
            std::printf(" %s", f.name);
            any = true;
        }
    }
    NvU64 known = 0;
    for (const FlagName& f : kFlags) {
        known |= f.bit;
    }
    if ((mask & ~known) != 0u) {
        std::printf(" +unknown(0x%llX)", static_cast<unsigned long long>(mask & ~known));
        any = true;
    }
    std::printf("%s\n", any ? "" : " (none)");
}

int NgxState(unsigned long pid) {
    std::printf("fl-probe-nvapi --ngx-state %lu — the driver's NGX feedback for one process\n\n", pid);
    const NvAPI_Status init = NvAPI_Initialize();
    if (init != NVAPI_OK) {
        NvAPI_ShortString desc = {};
        NvAPI_GetErrorMessage(init, desc);
        std::printf("  NvAPI_Initialize -> %d (%s)\n  BRANCH: DEGRADED — no usable NVIDIA driver on this machine.\n",
                    static_cast<int>(init), desc);
        std::printf("NGXSTATE status=DEGRADED nvapi=%d\n", static_cast<int>(init));
        return 2;
    }
    NvU32             driverVersion = 0;
    NvAPI_ShortString branch = {};
    if (NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, branch) == NVAPI_OK) {
        std::printf("  driver %u.%02u (%s)\n", driverVersion / 100, driverVersion % 100, branch);
    }

    NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS params = {};
    params.version = NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS_VER;
    params.processIdentifier = static_cast<NvU32>(pid);
    const NvAPI_Status status = NvAPI_NGX_GetNGXOverrideState(&params);
    if (status != NVAPI_OK) {
        NvAPI_ShortString desc = {};
        NvAPI_GetErrorMessage(status, desc);
        std::printf("  NvAPI_NGX_GetNGXOverrideState -> %d (%s)\n", static_cast<int>(status), desc);
        std::printf("  BRANCH: UNANSWERED — the driver did not answer for pid %lu (not an NVIDIA-rendered process, a "
                    "driver older than R570, or the API refusing this caller).\n",
                    pid);
        std::printf("NGXSTATE status=UNANSWERED nvapi=%d\n", static_cast<int>(status));
        NvAPI_Unload();
        return 3;
    }

    std::printf("  NvAPI_NGX_GetNGXOverrideState -> NVAPI_OK\n  BRANCH: ANSWERED\n\n");
    PrintMask("SR", params.feedbackMaskSR);
    PrintMask("RR", params.feedbackMaskRR);
    PrintMask("FG", params.feedbackMaskFG);
    std::printf("  scalingRatio          %.4f\n", static_cast<double>(params.scalingRatio));
    std::printf("  performanceMode       %u\n", params.performanceMode);
    std::printf("  renderPreset          %u\n", params.renderPreset);
    std::printf("  frameGenerationCount  %u   (\"FG Override Frame Count Target\" per nvapi.h -- whether this is the "
                "title's multiplier is what this run measures)\n",
                params.frameGenerationCount);
    std::printf("  frameGenerationPreset %u\n", params.frameGenerationPreset);
    std::printf("  frameGenerationMode   %u\n", params.frameGenerationMode);
    std::printf("  reserved              %u %u\n", params.reserved[0], params.reserved[1]);
    // THE MACHINE LINE. FrameLedger.CaptureHost spawns this probe beside each module snapshot
    // (NgxDriverProbe) and reads exactly this line; everything above is for a human. One line,
    // key=value, hex masks, so a parser cannot mistake a decoded name for a field.
    std::printf(
        "NGXSTATE status=ANSWERED sr=0x%016llX rr=0x%016llX fg=0x%016llX ratio=%.4f mode=%u preset=%u fgcount=%u "
        "fgpreset=%u fgmode=%u driver=%u\n",
        static_cast<unsigned long long>(params.feedbackMaskSR), static_cast<unsigned long long>(params.feedbackMaskRR),
        static_cast<unsigned long long>(params.feedbackMaskFG), static_cast<double>(params.scalingRatio),
        params.performanceMode, params.renderPreset, params.frameGenerationCount, params.frameGenerationPreset,
        params.frameGenerationMode, driverVersion);
    NvAPI_Unload();
    return 0;
}

}    // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--ngx-state") == 0) {
        return NgxState(std::strtoul(argv[2], nullptr, 10));
    }

    std::printf("fl-probe-nvapi — the vendored NVAPI material, exercised\n\n");

    // 1 + 2 are proven by this translation unit existing: it includes nvapi.h
    // and links nvapi64.lib. Stated rather than asserted, because asserting
    // "the build succeeded" from inside the built binary is the shape of an
    // assertion that cannot be false.
    std::printf("=== 1. header closure + x64 link ===\n");
    std::printf("  [PASS] nvapi.h and its includes resolved; amd64/nvapi64.lib linked x64\n");
    std::printf("         (proven by this binary existing, not by a runtime check)\n\n");

    std::printf("=== 2. NvAPI_Initialize, and which branch we took ===\n");
    const NvAPI_Status status = NvAPI_Initialize();

    if (status != NVAPI_OK) {
        // The degradation path. This is the CI outcome and it is a PASS.
        NvAPI_ShortString desc = {};
        // GetErrorMessage is safe before a successful Initialize.
        NvAPI_GetErrorMessage(status, desc);
        std::printf("  NvAPI_Initialize -> %d (%s)\n", static_cast<int>(status), desc);
        std::printf("  BRANCH: DEGRADED — no usable NVIDIA driver on this machine.\n");
        Check(true, "L3 unavailability is a normal condition, reported and survived");
        std::printf("\n  NOT MEASURED HERE: driver version, GPU name. A machine with an\n");
        std::printf("  NVIDIA driver takes the other branch and measures them.\n");
        std::printf("\n%s\n", g_failed ? "FAILED" : "OK (degraded branch)");
        return g_failed ? 1 : 0;
    }

    std::printf("  NvAPI_Initialize -> NVAPI_OK\n");
    std::printf("  BRANCH: AVAILABLE — an NVIDIA driver answered.\n\n");

    std::printf("=== 3. the two calls §L3 needs before anything else ===\n");

    NvU32              driverVersion = 0;
    NvAPI_ShortString  branch = {};
    const NvAPI_Status verStatus = NvAPI_SYS_GetDriverAndBranchVersion(&driverVersion, branch);
    Check(verStatus == NVAPI_OK, "NvAPI_SYS_GetDriverAndBranchVersion");
    if (verStatus == NVAPI_OK) {
        std::printf("         driver %u.%02u, branch %s\n", driverVersion / 100, driverVersion % 100, branch);
    }

    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvU32               gpuCount = 0;
    const NvAPI_Status  enumStatus = NvAPI_EnumPhysicalGPUs(gpus, &gpuCount);
    Check(enumStatus == NVAPI_OK, "NvAPI_EnumPhysicalGPUs");

    // A driver that answers with ZERO GPUs is not a success. Without this the
    // loop below runs zero times and every assertion inside it is vacuous —
    // the "a filter that selected nothing exits 0" defect this project has hit
    // before (docs/20_OPEN_QUESTIONS.md §S29, gates-that-cannot-fail).
    Check(enumStatus != NVAPI_OK || gpuCount > 0, "at least one physical GPU enumerated");
    std::printf("         %u physical GPU(s)\n", gpuCount);

    for (NvU32 i = 0; i < gpuCount; ++i) {
        NvAPI_ShortString  name = {};
        const NvAPI_Status nameStatus = NvAPI_GPU_GetFullName(gpus[i], name);
        Check(nameStatus == NVAPI_OK, "NvAPI_GPU_GetFullName");
        if (nameStatus == NVAPI_OK) {
            std::printf("         gpu[%u] %s\n", i, name);
        }
    }

    NvAPI_Unload();

    std::printf("\n%s\n", g_failed ? "FAILED" : "OK (available branch)");
    return g_failed ? 1 : 0;
}
