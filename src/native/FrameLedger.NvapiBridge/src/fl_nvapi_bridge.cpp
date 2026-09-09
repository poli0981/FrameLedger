// fl_nvapi_bridge.cpp — see fl_nvapi_bridge.h.
//
// EVERY NVAPI CALL HERE IS A READ. 18_GPU_VENDOR_APIS §Runtime policy: the libraries behind the
// layers can also set clocks, fan curves and power limits; FrameLedger never calls a setter, and
// this translation unit is the whole of what links nvapi64.lib into a shipped binary, so the claim
// is checkable by reading one file.
#define FL_NV_BUILDING
#include "fl_nvapi_bridge.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "nvapi.h"

namespace {

// Initialise/shutdown are serialised; reads are independent NVAPI calls on a handle that is set
// once and never changes while the count is above zero.
CRITICAL_SECTION    g_lock;
bool                g_lockInit = false;
int                 g_refs = 0;
NvPhysicalGpuHandle g_gpu = nullptr;
bool                g_haveGpu = false;
std::atomic<bool>   g_ready{false};

void EnsureLock() {
    // DllMain is not used (no work under the loader lock); the first caller creates the section.
    // A race between two first callers is not a shape the managed side produces — the facade is
    // constructed once — and the section is never deleted, so a second init would only leak one.
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
}

struct Locked {
    Locked() {
        EnsureLock();
        EnterCriticalSection(&g_lock);
    }
    ~Locked() { LeaveCriticalSection(&g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

void ReadThermal(FlNvSample& s) {
    NV_GPU_THERMAL_SETTINGS t{};
    t.version = NV_GPU_THERMAL_SETTINGS_VER;
    if (NvAPI_GPU_GetThermalSettings(g_gpu, NVAPI_THERMAL_TARGET_ALL, &t) != NVAPI_OK) {
        return;
    }
    for (NvU32 i = 0; i < t.count && i < NVAPI_MAX_THERMAL_SENSORS_PER_GPU; ++i) {
        if (t.sensor[i].target == NVAPI_THERMAL_TARGET_GPU && (s.present & FL_NV_FIELD_TEMP_CORE) == 0u) {
            s.tempCoreC = static_cast<float>(t.sensor[i].currentTemp);
            s.present |= FL_NV_FIELD_TEMP_CORE;
        } else if (t.sensor[i].target == NVAPI_THERMAL_TARGET_MEMORY && (s.present & FL_NV_FIELD_TEMP_MEMORY) == 0u) {
            s.tempMemoryC = static_cast<float>(t.sensor[i].currentTemp);
            s.present |= FL_NV_FIELD_TEMP_MEMORY;
        }
    }
}

void ReadLoad(FlNvSample& s) {
    NV_GPU_DYNAMIC_PSTATES_INFO_EX p{};
    p.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
    if (NvAPI_GPU_GetDynamicPstatesInfoEx(g_gpu, &p) != NVAPI_OK) {
        return;
    }
    // Index 0 is the GPU domain: nvapi.h documents utilization[NVAPI_GPU_UTILIZATION_DOMAIN_GPU] and
    // defines no such constant in this vintage of the header, so the documented index is spelled out.
    constexpr NvU32 kGpuDomain = 0;
    if (p.utilization[kGpuDomain].bIsPresent != 0u) {
        s.loadPct = static_cast<float>(p.utilization[kGpuDomain].percentage);
        s.present |= FL_NV_FIELD_LOAD;
    }
}

void ReadClocks(FlNvSample& s) {
    NV_GPU_CLOCK_FREQUENCIES c{};
    c.version = NV_GPU_CLOCK_FREQUENCIES_VER;
    c.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
    if (NvAPI_GPU_GetAllClockFrequencies(g_gpu, &c) != NVAPI_OK) {
        return;
    }
    if (c.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent != 0u) {
        s.coreClockMhz = static_cast<float>(c.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency) / 1000.0f;
        s.present |= FL_NV_FIELD_CORE_CLOCK;
    }
    if (c.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent != 0u) {
        s.memClockMhz = static_cast<float>(c.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency) / 1000.0f;
        s.present |= FL_NV_FIELD_MEM_CLOCK;
    }
}

void ReadExtras(FlNvSample& s) {
    NvU32 v = 0;
    if (NvAPI_GPU_GetTachReading(g_gpu, &v) == NVAPI_OK) {
        s.fanRpm = static_cast<float>(v);
        s.present |= FL_NV_FIELD_FAN;
    }
    v = 0;
    if (NvAPI_GPU_GetPerfDecreaseInfo(g_gpu, &v) == NVAPI_OK) {
        s.throttleReasons = v;
        s.present |= FL_NV_FIELD_THROTTLE;
    }
    v = 0;
    if (NvAPI_GPU_GetCurrentPCIEDownstreamWidth(g_gpu, &v) == NVAPI_OK && v != 0u) {
        s.pcieWidth = v;
        s.present |= FL_NV_FIELD_PCIE_WIDTH;
    }
    NvAPI_ShortString name{};
    if (NvAPI_GPU_GetFullName(g_gpu, name) == NVAPI_OK) {
        std::memcpy(s.name, name, sizeof(s.name) - 1);
        s.name[sizeof(s.name) - 1] = '\0';
        s.present |= FL_NV_FIELD_NAME;
    }
}

}    // namespace

extern "C" {

FL_NV_API int32_t FlNvInit(void) {
    Locked lock;
    if (g_refs > 0) {
        ++g_refs;
        return g_haveGpu ? FL_NV_OK : FL_NV_NO_GPU;
    }
    const NvAPI_Status init = NvAPI_Initialize();
    if (init != NVAPI_OK) {
        return static_cast<int32_t>(init);
    }
    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS]{};
    NvU32               count = 0;
    g_haveGpu = NvAPI_EnumPhysicalGPUs(handles, &count) == NVAPI_OK && count > 0u;
    g_gpu = g_haveGpu ? handles[0] : nullptr;
    g_refs = 1;
    g_ready.store(true, std::memory_order_release);
    return g_haveGpu ? FL_NV_OK : FL_NV_NO_GPU;
}

FL_NV_API void FlNvShutdown(void) {
    Locked lock;
    if (g_refs == 0) {
        return;
    }
    if (--g_refs == 0) {
        g_ready.store(false, std::memory_order_release);
        g_gpu = nullptr;
        g_haveGpu = false;
        NvAPI_Unload();
    }
}

FL_NV_API int32_t FlNvReadSample(FlNvSample* out) {
    if (out == nullptr) {
        return FL_NV_BAD_ARGUMENT;
    }
    if (out->size != sizeof(FlNvSample)) {
        return FL_NV_BAD_SIZE;
    }
    if (!g_ready.load(std::memory_order_acquire)) {
        return FL_NV_NOT_INITIALISED;
    }
    Locked lock;
    if (!g_haveGpu) {
        return FL_NV_NO_GPU;
    }
    FlNvSample s{};
    s.size = sizeof(FlNvSample);
    ReadThermal(s);
    ReadLoad(s);
    ReadClocks(s);
    ReadExtras(s);
    *out = s;
    return FL_NV_OK;
}

FL_NV_API int32_t FlNvNgxState(uint32_t pid, FlNvNgxWords* out) {
    if (out == nullptr) {
        return FL_NV_BAD_ARGUMENT;
    }
    if (out->size != sizeof(FlNvNgxWords)) {
        return FL_NV_BAD_SIZE;
    }
    FlNvNgxWords r{};
    r.size = sizeof(FlNvNgxWords);
    if (!g_ready.load(std::memory_order_acquire)) {
        r.status = FL_NV_NGX_DEGRADED;
        r.nvapiStatus = FL_NV_NOT_INITIALISED;
        *out = r;
        return FL_NV_OK;
    }
    Locked            lock;
    NvU32             driver = 0;
    NvAPI_ShortString branch{};
    if (NvAPI_SYS_GetDriverAndBranchVersion(&driver, branch) == NVAPI_OK) {
        r.driver = driver;
    }
    NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS params{};
    params.version = NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS_VER;
    params.processIdentifier = static_cast<NvU32>(pid);
    const NvAPI_Status status = NvAPI_NGX_GetNGXOverrideState(&params);
    r.nvapiStatus = static_cast<int32_t>(status);
    if (status != NVAPI_OK) {
        r.status = FL_NV_NGX_UNANSWERED;
        *out = r;
        return FL_NV_OK;
    }
    r.status = FL_NV_NGX_ANSWERED;
    r.sr = params.feedbackMaskSR;
    r.rr = params.feedbackMaskRR;
    r.fg = params.feedbackMaskFG;
    r.scalingRatio = params.scalingRatio;
    r.performanceMode = params.performanceMode;
    r.renderPreset = params.renderPreset;
    r.frameGenerationCount = params.frameGenerationCount;
    r.frameGenerationPreset = params.frameGenerationPreset;
    r.frameGenerationMode = params.frameGenerationMode;
    *out = r;
    return FL_NV_OK;
}

FL_NV_API int32_t FlNvDriverVersion(uint32_t* version, char* branch, uint32_t branchCapacity) {
    if (version == nullptr) {
        return FL_NV_BAD_ARGUMENT;
    }
    if (!g_ready.load(std::memory_order_acquire)) {
        return FL_NV_NOT_INITIALISED;
    }
    Locked             lock;
    NvU32              v = 0;
    NvAPI_ShortString  b{};
    const NvAPI_Status status = NvAPI_SYS_GetDriverAndBranchVersion(&v, b);
    if (status != NVAPI_OK) {
        return static_cast<int32_t>(status);
    }
    *version = v;
    if (branch != nullptr && branchCapacity > 0u) {
        const size_t n = std::strlen(b);
        const size_t copy = n < branchCapacity - 1u ? n : branchCapacity - 1u;
        std::memcpy(branch, b, copy);
        branch[copy] = '\0';
    }
    return FL_NV_OK;
}

FL_NV_API uint32_t FlNvAbiVersion(void) {
    return FL_NV_ABI_VERSION;
}

FL_NV_API uint32_t FlNvSampleSize(void) {
    return sizeof(FlNvSample);
}

FL_NV_API uint32_t FlNvNgxStateSize(void) {
    return sizeof(FlNvNgxWords);
}

FL_NV_API const char* FlNvBuildId(void) {
    return FL_BUILD_ID;
}

}    // extern "C"
