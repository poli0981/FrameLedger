// P0 build-profile probes — docs/20_OPEN_QUESTIONS.md §H1 and §H3.
//
// These answer questions that change the build configuration of the ENTIRE
// native layer, so they run before hook code depends on the answer. Neither
// needs a GPU or a real game: Control Flow Guard does not care which function
// was hooked, only how the call to the trampoline is made. That makes the
// result deterministic and CI-runnable.
//
// This target is compiled with the exact flags 17_HOOK_ENGINE mandates for the
// Overlay — /guard:cf /GS /Qspectre /GR- /EHsc- /MT and -D_HAS_EXCEPTIONS=0 —
// because a probe built with different flags would prove nothing about them.
//
// Exit code 0 = every probe passed. A CFG violation does not return an exit
// code at all: __fastfail terminates the process immediately and uncatchably,
// which is itself the observation we are making.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <MinHook.h>

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// H1 — /guard:cf versus MinHook trampolines.
//
// The concern: MinHook allocates trampoline memory at runtime with
// VirtualAlloc. Calling it goes through a function pointer, i.e. an INDIRECT
// call, and a /guard:cf-compiled module emits a _guard_check_icall before every
// indirect call. If runtime-allocated executable memory is not a valid call
// target in the process CFG bitmap, that check fails with
// __fastfail(FAST_FAIL_GUARD_ICALL_CHECK_FAILURE) — which kills the host game
// and cannot be caught by SEH, directly contradicting NFR-3.
// ---------------------------------------------------------------------------

using TargetFn = int(__stdcall*)(int);

// __declspec(noinline) so the call actually happens; MinHook needs a real
// function body with enough bytes to patch.
__declspec(noinline) int __stdcall TargetFunction(int x) {
    volatile int acc = x;
    acc += 1;
    acc *= 2;
    return acc;
}

TargetFn g_original = nullptr;
bool     g_detourRan = false;

int __stdcall DetourFunction(int x) {
    g_detourRan = true;
    // THE point of the probe: an indirect call, from a /guard:cf module, to
    // MinHook's runtime-allocated trampoline. If CFG rejects it, we never
    // return from this line — the process dies here.
    return g_original(x) + 100;
}

bool ProbeH1_GuardCfTrampoline() {
    std::printf("\nH1 — /guard:cf indirect call into a MinHook trampoline\n");

    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
    const bool                                   gotPolicy =
        GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfg, sizeof(cfg)) != 0;
    std::printf("  process CFG enabled: %s   strict mode: %s\n",
                (gotPolicy && cfg.EnableControlFlowGuard) ? "YES" : "no", (gotPolicy && cfg.StrictMode) ? "YES" : "no");

    // Without this the probe is theatre: if CFG is not actually enforcing,
    // the indirect call below succeeds for reasons that say nothing about H1.
    Check(gotPolicy && cfg.EnableControlFlowGuard != 0, "CFG is actually enforcing for this process");

    if (MH_Initialize() != MH_OK) {
        Check(false, "MH_Initialize");
        return false;
    }

    const int expectedOriginal = TargetFunction(21);

    if (MH_CreateHook(reinterpret_cast<LPVOID>(&TargetFunction), reinterpret_cast<LPVOID>(&DetourFunction),
                      reinterpret_cast<LPVOID*>(&g_original)) != MH_OK) {
        Check(false, "MH_CreateHook");
        MH_Uninitialize();
        return false;
    }
    if (MH_EnableHook(reinterpret_cast<LPVOID>(&TargetFunction)) != MH_OK) {
        Check(false, "MH_EnableHook");
        MH_Uninitialize();
        return false;
    }

    // If CFG rejects the trampoline, execution ends inside this call.
    const int hooked = TargetFunction(21);

    Check(g_detourRan, "detour was entered");
    Check(hooked == expectedOriginal + 100, "trampoline returned the original result");
    std::printf("  original=%d hooked=%d (survived the indirect call)\n", expectedOriginal, hooked);

    // Also exercise the uninstall path, which restores the patched bytes.
    Check(MH_DisableHook(reinterpret_cast<LPVOID>(&TargetFunction)) == MH_OK, "MH_DisableHook");
    Check(TargetFunction(21) == expectedOriginal, "original behaviour restored after unhook");
    MH_Uninitialize();
    return true;
}

// ---------------------------------------------------------------------------
// H3 — -D_HAS_EXCEPTIONS=0 with <atomic>.
//
// Compiling this file at all is most of the answer: it is built with the define
// and includes <atomic> via the seqlock below. What the runtime check adds is
// that atomic_ref is LOCK-FREE at both widths — a non-lock-free atomic would
// take a mutex, and a mutex in the present hook violates CLAUDE.md rule 5.
// ---------------------------------------------------------------------------

bool ProbeH3_AtomicRefWithoutExceptions() {
    std::printf("\nH3 — <atomic> under -D_HAS_EXCEPTIONS=0\n");

#ifdef _HAS_EXCEPTIONS
    std::printf("  _HAS_EXCEPTIONS = %d\n", static_cast<int>(_HAS_EXCEPTIONS));
    Check(_HAS_EXCEPTIONS == 0, "built with exceptions disabled (probe is meaningless otherwise)");
#else
    Check(false, "_HAS_EXCEPTIONS not defined — probe not built as intended");
#endif

    alignas(64) struct {
        std::uint64_t writeIndex;
        std::uint32_t seq;
        std::uint32_t pad;
    } slot{};

    std::atomic_ref<std::uint32_t> seq{slot.seq};
    std::atomic_ref<std::uint64_t> wi{slot.writeIndex};

    Check(seq.is_lock_free(), "atomic_ref<uint32_t> is lock-free");
    Check(wi.is_lock_free(), "atomic_ref<uint64_t> is lock-free");

    // The exact writer sequence from 07_IPC §Protocol rules.
    const std::uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    seq.store(s + 2, std::memory_order_relaxed);
    wi.store(1, std::memory_order_release);

    Check(slot.seq == 2 && slot.writeIndex == 1, "seqlock write sequence executed");
    return true;
}

// ---------------------------------------------------------------------------
// H2 — installing hooks from inside a LoadLibrary hook.
//
// 17_HOOK_ENGINE installs a LoadLibrary hook so graphics DLLs loaded LATER
// still get hooked (many games load D3D12 lazily). The hazard: that hook body
// runs while the loader lock is held, and MH_EnableHook suspends every other
// thread to patch. Suspend a thread that holds or is waiting on the loader
// lock and the process deadlocks — inside somebody else's game.
//
// WHAT THIS PROBE PROVES, AND WHAT IT DOES NOT.
//
// Proves: the DEFERRED pattern is safe. Thread-suspending patches can be run
// repeatedly while another thread hammers the loader, without deadlock.
//
// Does NOT prove: that the naive inline install deadlocks. A probe that
// deadlocks cannot report its own result, and deliberately hanging CI to
// demonstrate a hazard we have already decided to avoid buys nothing. The case
// against inline installation rests on the documented mechanism — suspending a
// thread that holds the loader lock — not on this probe.
//
// So: the safe path is verified, the unsafe path is argued. Do not upgrade
// that claim when writing this up.
// ---------------------------------------------------------------------------

std::atomic<bool> g_loaderThreadRun{true};
std::atomic<int>  g_loadCount{0};

DWORD WINAPI LoaderChurnThread(LPVOID) {
    // Keep the loader lock hot so a suspend-while-held is likely rather than
    // theoretical. These are tiny, always-present system DLLs.
    static const wchar_t* kLibs[] = {L"version.dll", L"psapi.dll", L"winmm.dll"};
    int                   i = 0;
    while (g_loaderThreadRun.load(std::memory_order_relaxed)) {
        HMODULE m = LoadLibraryW(kLibs[i++ % 3]);
        if (m) {
            g_loadCount.fetch_add(1, std::memory_order_relaxed);
            FreeLibrary(m);
        }
    }
    return 0;
}

// The deferred-install queue 17_HOOK_ENGINE §DLL entry now mandates: the
// LoadLibrary hook only records what it saw; the init thread installs later,
// outside the loader lock.
std::atomic<int> g_deferredRequests{0};

bool ProbeH2_LoaderLockDeferral() {
    std::printf("\nH2 — hook installation from inside a LoadLibrary hook\n");

    HANDLE churn = CreateThread(nullptr, 0, &LoaderChurnThread, nullptr, 0, nullptr);
    if (!churn) {
        Check(false, "spawn loader-churn thread");
        return false;
    }
    Sleep(50);    // let it get going

    // Stand-in for the hook body: enqueue only, never patch here.
    for (int i = 0; i < 100; ++i) {
        g_deferredRequests.fetch_add(1, std::memory_order_relaxed);
    }

    // Now do the dangerous operation (thread-suspending patch) from a normal
    // thread that does NOT hold the loader lock — the deferred pattern.
    bool installedSafely = true;
    if (MH_Initialize() == MH_OK) {
        for (int round = 0; round < 20 && installedSafely; ++round) {
            if (MH_CreateHook(reinterpret_cast<LPVOID>(&TargetFunction), reinterpret_cast<LPVOID>(&DetourFunction),
                              reinterpret_cast<LPVOID*>(&g_original)) != MH_OK) {
                installedSafely = false;
                break;
            }
            // MH_EnableHook is what suspends every other thread, including the
            // churn thread that is repeatedly inside the loader.
            if (MH_EnableHook(reinterpret_cast<LPVOID>(&TargetFunction)) != MH_OK ||
                MH_DisableHook(reinterpret_cast<LPVOID>(&TargetFunction)) != MH_OK ||
                MH_RemoveHook(reinterpret_cast<LPVOID>(&TargetFunction)) != MH_OK) {
                installedSafely = false;
            }
        }
        MH_Uninitialize();
    } else {
        installedSafely = false;
    }

    g_loaderThreadRun.store(false, std::memory_order_relaxed);
    const DWORD waited = WaitForSingleObject(churn, 5000);
    CloseHandle(churn);

    std::printf("  loader churn completed %d LoadLibrary/FreeLibrary cycles\n",
                g_loadCount.load(std::memory_order_relaxed));
    std::printf("  deferred install requests queued: %d\n", g_deferredRequests.load(std::memory_order_relaxed));

    Check(waited == WAIT_OBJECT_0, "churn thread joined (no deadlock under repeated suspend-to-patch)");
    Check(installedSafely, "20 enable/disable cycles while the loader was busy");
    Check(g_deferredRequests.load(std::memory_order_relaxed) == 100, "deferred queue recorded every request");
    return true;
}

}    // namespace

int main() {
    std::printf("FrameLedger P0 build-profile probes\n");
    std::printf("(20_OPEN_QUESTIONS H1, H2, H3 — results recorded in docs/spike-notes.md)\n");

    ProbeH3_AtomicRefWithoutExceptions();
    ProbeH1_GuardCfTrampoline();
    ProbeH2_LoaderLockDeferral();

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PROBES PASSED" : "PROBE FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
