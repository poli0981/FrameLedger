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

#include "nvapi.h"

namespace {

bool g_failed = false;

void Check(bool condition, const char* what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition) {
        g_failed = true;
    }
}

}    // namespace

int main() {
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
