// fl_nvapi_bridge.h — the C ABI the managed Agent reaches NVAPI through (docs/18_GPU_VENDOR_APIS.md §L3).
//
// WHAT THIS IS. NVAPI is a C API behind a vendored import library, and CLAUDE.md puts the native
// boundary in FrameLedger.Infrastructure: this DLL is the second P/Invoke facade after the guard's,
// and it is READ-ONLY by construction — nothing here calls a setter, and nothing here is reachable
// from a game process. It is loaded by the Agent (and the unshipped capture host) BESIDE the
// binary, by absolute path, never into a target: the name deliberately carries none of the words
// the guard's own §S18 heuristic scans a launcher's ancestors for.
//
// WHAT IT IS NOT. Not Reflex latency: NvAPI_D3D_GetLatency is per frame and per device, and the
// Overlay's in-process hook is where that fact lives (20_OPEN_QUESTIONS §M8). Not a poller: the
// managed TelemetryPoller asks FlNvReadSample at 1 Hz; this answers what the driver says now.
//
// VERSIONED STRUCTS. Every out-struct carries its own size as the first field, set by the caller and
// checked here, so a managed mirror that drifts from this header is refused (FL_NV_BAD_SIZE) rather
// than read as garbage — the same rule fl_shm.h applies with its static_asserts, one step later.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FL_NV_BUILDING
#define FL_NV_API __declspec(dllexport)
#else
#define FL_NV_API __declspec(dllimport)
#endif

#define FL_NV_ABI_VERSION 1u

// Status codes. Zero is success; negatives are ours; an NvAPI_Status is passed through where the
// caller can act on it (it is negative too, and never one of ours).
#define FL_NV_OK 0
#define FL_NV_NOT_INITIALISED (-1000)
#define FL_NV_BAD_SIZE (-1001)
#define FL_NV_NO_GPU (-1002)
#define FL_NV_BAD_ARGUMENT (-1003)

// Which fields of FlNvSample carry a value. A clear bit is N/A, never 0 (03_METRICS §Sensor aggregates).
#define FL_NV_FIELD_TEMP_CORE 0x001u
#define FL_NV_FIELD_TEMP_MEMORY 0x002u
#define FL_NV_FIELD_LOAD 0x004u
#define FL_NV_FIELD_VRAM                                                                                               \
    0x008u    // reserved: the vendored headers carry no memory-info entry point (L1 PDH and L2 own it)
#define FL_NV_FIELD_CORE_CLOCK 0x010u
#define FL_NV_FIELD_MEM_CLOCK 0x020u
#define FL_NV_FIELD_FAN 0x040u
#define FL_NV_FIELD_THROTTLE 0x080u
#define FL_NV_FIELD_PCIE_WIDTH 0x100u
#define FL_NV_FIELD_NAME 0x200u

typedef struct FlNvSample {
    uint32_t size;       // sizeof(FlNvSample), set by the caller
    uint32_t present;    // FL_NV_FIELD_* bits
    float    tempCoreC;
    float    tempMemoryC;
    float    loadPct;       // NVAPI_GPU_UTILIZATION_DOMAIN_GPU, last 1 s
    float    vramUsedMb;    // reserved (FL_NV_FIELD_VRAM is never set by this build)
    float    coreClockMhz;
    float    memClockMhz;
    float    fanRpm;
    uint32_t throttleReasons;    // NV_GPU_PERF_DECREASE_REASON_* bits, raw
    uint32_t pcieWidth;
    uint32_t reserved[4];
    char     name[64];    // NvAPI_ShortString, NUL-terminated
} FlNvSample;

// NvAPI_NGX_GetNGXOverrideState for one process (R570+): the driver's own per-process NGX words.
#define FL_NV_NGX_ANSWERED 0
#define FL_NV_NGX_UNANSWERED 1    // NvAPI answered but not for this pid
#define FL_NV_NGX_DEGRADED 2      // no usable NVIDIA driver

typedef struct FlNvNgxWords {
    uint32_t size;           // sizeof(FlNvNgxWords), set by the caller
    int32_t  status;         // FL_NV_NGX_*
    int32_t  nvapiStatus;    // the NvAPI_Status behind status, verbatim
    uint32_t driver;         // NvAPI_SYS_GetDriverAndBranchVersion, e.g. 61664
    uint64_t sr;             // feedbackMaskSR
    uint64_t rr;             // feedbackMaskRR
    uint64_t fg;             // feedbackMaskFG
    float    scalingRatio;
    uint32_t performanceMode;
    uint32_t renderPreset;
    uint32_t frameGenerationCount;
    uint32_t frameGenerationPreset;
    uint32_t frameGenerationMode;
    uint32_t reserved[2];
} FlNvNgxWords;

// Reference-counted: every FlNvInit is matched by an FlNvShutdown, and NvAPI_Unload runs at zero.
// Returns FL_NV_OK, or the NvAPI_Status NvAPI_Initialize answered (a normal condition on a machine
// with no NVIDIA driver — L3 disables cleanly, never throws). A machine whose driver initialises
// but enumerates no GPU answers FL_NV_NO_GPU.
FL_NV_API int32_t FlNvInit(void);
FL_NV_API void    FlNvShutdown(void);

// One reading of the first physical GPU. Each field is independent: a call the driver refuses
// leaves its bit clear and the others intact. FL_NV_NOT_INITIALISED / FL_NV_BAD_SIZE otherwise.
FL_NV_API int32_t FlNvReadSample(FlNvSample* out);

// The driver's NGX words for `pid`. Never fails for a reason the caller cannot read: `status` says
// which branch was taken, and `nvapiStatus` says why.
FL_NV_API int32_t FlNvNgxState(uint32_t pid, FlNvNgxWords* out);

// Driver version (e.g. 61664) and branch string; FL_NV_NOT_INITIALISED before FlNvInit.
FL_NV_API int32_t FlNvDriverVersion(uint32_t* version, char* branch, uint32_t branchCapacity);

// What the managed mirror tests compare against, and what a build id check reads.
FL_NV_API uint32_t    FlNvAbiVersion(void);
FL_NV_API uint32_t    FlNvSampleSize(void);
FL_NV_API uint32_t    FlNvNgxStateSize(void);
FL_NV_API const char* FlNvBuildId(void);

#ifdef __cplusplus
}
#endif
