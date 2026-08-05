// FrameLedger.Overlay — the injected DLL.
//
// THIS SLICE: DllMain, the init thread, the shared mapping, and the handshake.
// NO HOOKS YET. `status` stays FL_STATUS_INIT precisely because nothing is
// hooked; publishing FL_STATUS_READY here would tell the Agent a capture side
// exists when the ring will never receive a record. The present hook and the
// fault policy land in the next change, and `status` moves with them.
//
// docs/17_HOOK_ENGINE.md is the specification. The constraints that shape every
// line:
//
//   - DllMain(DLL_PROCESS_ATTACH) does ONLY DisableThreadLibraryCalls and
//     CreateThread -> InitThread. Loader-lock rules: anything else here runs
//     while the loader lock is held, and MinHook suspends every other thread to
//     patch (20_OPEN_QUESTIONS §H2).
//   - Every hook body will be SEH-guarded, allocation-free, lock-free and log
//     nothing. Three faults => self-disable and go dormant.
//   - Read nothing but the arguments of APIs we hooked (CLAUDE.md rule 4).
//   - -D_HAS_EXCEPTIONS=0 turns a would-be STL throw into __fastfail, which SEH
//     cannot intercept, so "no throwing STL" is load-bearing rather than
//     stylistic (spike-notes.md §H3).

#include <windows.h>

#include <cstdio>
#include <fl_ring.h>
#include <fl_shm.h>
#include <sddl.h>

using namespace fl;

namespace {

HANDLE         g_mapping = nullptr;
void*          g_base = nullptr;
fl::RingWriter g_writer;
HANDLE         g_initThread = nullptr;

// The mapping is created with a DACL granting ONLY the current user's SID, and
// lives in the session-scoped Local\ namespace (docs/07_IPC.md §Security). No
// Global\ object: it would need admin and would be visible across sessions for
// no benefit.
//
// Returns false rather than falling back to a default DACL. A mapping the whole
// machine can write is not a degraded version of this one -- the Agent's control
// block is in it, and unhookRequested is the safety stop.
bool BuildUserOnlySecurity(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd) noexcept {
    sd = nullptr;

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    unsigned char buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    DWORD         got = 0;
    const BOOL    ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &got);
    CloseHandle(token);
    if (!ok) {
        return false;
    }

    LPWSTR      sidText = nullptr;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer);
    if (!ConvertSidToStringSidW(user->User.Sid, &sidText)) {
        return false;
    }

    // D:P = a protected DACL, so nothing is inherited in. One ACE, generic all,
    // for us alone.
    wchar_t   sddl[256]{};
    const int n = _snwprintf_s(sddl, _TRUNCATE, L"D:P(A;;GA;;;%ls)", sidText);
    LocalFree(sidText);
    if (n < 0) {
        return false;
    }

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

// Local\FrameLedger.Ring.<pid>. Deliberately plain and identifiable: 19_SAFETY
// forbids obfuscated object names, because being visible to anti-cheat is the
// whole design posture.
bool MakeRingName(wchar_t* out, size_t cap, DWORD pid) noexcept {
    return _snwprintf_s(out, cap, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", pid) >= 0;
}

bool CreateRing() noexcept {
    SECURITY_ATTRIBUTES  sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!BuildUserOnlySecurity(sa, sd)) {
        return false;
    }

    wchar_t name[128]{};
    if (!MakeRingName(name, 128, GetCurrentProcessId())) {
        LocalFree(sd);
        return false;
    }

    const size_t bytes = fl::FlShmSizeForCapacity(FL_SHM_DEFAULT_CAPACITY);
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, static_cast<DWORD>(bytes >> 32),
                                   static_cast<DWORD>(bytes & 0xFFFFFFFFu), name);
    const DWORD err = GetLastError();
    LocalFree(sd);
    if (g_mapping == nullptr) {
        return false;
    }
    // A pre-existing mapping under our own pid is not a mapping we understand:
    // either a stale object or another writer. Refuse rather than share a ring
    // with an unknown producer.
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }

    g_base = MapViewOfFile(g_mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes);
    if (g_base == nullptr) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    return true;
}

// Write-once at init, per docs/07_IPC.md §A + B. adapterLuid is deliberately
// left 0 = "not yet known": this runs two steps before any graphics module is
// resolved, and our own dummy device's adapter is not the game's (#36). It is
// published at first present, with the hook.
void PublishHandshake() noexcept {
    auto* h = reinterpret_cast<FlShmHandshake*>(static_cast<unsigned char*>(g_base) + FL_SHM_HANDSHAKE_OFFSET);
    h->recordSize = sizeof(FlFrameRecord);
    h->capacity = FL_SHM_DEFAULT_CAPACITY;
    h->pid = GetCurrentProcessId();

    // FL_BUILD_ID comes from git describe at configure time (FrameLedger.Shm's
    // CMakeLists). It had no producer at all until #36; without one the Agent's
    // refuse-to-attach-on-mismatch compares "" with "" forever.
    const char* build = FL_BUILD_ID;
    size_t      i = 0;
    for (; i + 1 < sizeof(h->buildId) && build[i] != '\0'; ++i) {
        h->buildId[i] = build[i];
    }
    h->buildId[i] = '\0';

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    h->qpcEpoch = static_cast<uint64_t>(qpc.QuadPart);

    h->adapterLuid = 0;

    // layoutVersion LAST, with a release fence in front of it. It is the field a
    // reader validates first, so it must not become visible before the fields it
    // vouches for. A reader that saw the version while capacity was still zero
    // would compute a ring of no slots and read garbage.
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_ref<uint32_t> version{h->layoutVersion};
    version.store(FL_SHM_LAYOUT_VERSION, std::memory_order_release);
}

DWORD WINAPI InitThread(LPVOID) noexcept {
    if (!CreateRing()) {
        return 1;
    }
    PublishHandshake();

    auto* state = reinterpret_cast<FlWriterState*>(static_cast<unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    if (!g_writer.Init(g_base, FL_SHM_DEFAULT_CAPACITY)) {
        return 1;
    }

    // NOT FL_STATUS_READY. Nothing is hooked yet, so no record will ever arrive,
    // and READY would be a claim about a capture side that does not exist.
    std::atomic_ref<uint32_t> status{state->status};
    status.store(FL_STATUS_INIT, std::memory_order_release);
    return 0;
}

}    // namespace

// Exports keep their real names. Being identifiable to anti-cheat is a
// requirement, not an accident (docs/19_SAFETY_AND_ANTICHEAT.md).
extern "C" {

__declspec(dllexport) unsigned int FlGetLayoutVersion() {
    return FL_SHM_LAYOUT_VERSION;
}

// 17_HOOK_ENGINE §Build profile requires this export, and 07_IPC makes a build-id
// mismatch a hard refuse-to-attach.
__declspec(dllexport) const char* FlGetBuildId() {
    return FL_BUILD_ID;
}

__declspec(dllexport) unsigned int FlGetStatus() {
    if (g_base == nullptr) {
        return FL_STATUS_INIT;
    }
    const auto* state =
        reinterpret_cast<const FlWriterState*>(static_cast<const unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    std::atomic_ref<const uint32_t> status{state->status};
    return status.load(std::memory_order_acquire);
}

// The safety stop's local entry point. It does nothing yet because nothing is
// hooked; the body lands with the hooks, where 07_IPC requires it to be the
// fastest, most-tested path in the DLL. Declared now because the export list is
// part of what an anti-cheat vendor inspects, and a DLL whose exports change
// shape between builds is harder to identify, not easier.
__declspec(dllexport) void FlRequestUnhook() {
    if (g_base == nullptr) {
        return;
    }
    auto* state = reinterpret_cast<FlWriterState*>(static_cast<unsigned char*>(g_base) + FL_SHM_WRITER_OFFSET);
    std::atomic_ref<uint32_t> status{state->status};
    status.store(FL_STATUS_UNHOOKED, std::memory_order_release);
}

}    // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // ONLY these two calls. Everything else -- mapping creation, MinHook,
        // dummy-device creation, hook installation -- happens on the init thread,
        // outside the loader lock.
        DisableThreadLibraryCalls(module);
        g_initThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    // No DLL_PROCESS_DETACH teardown: 17_HOOK_ENGINE §Unhooking is explicit that
    // the DLL is never FreeLibrary'd from a live process, because a thread may
    // still be inside a trampoline. It goes dormant and unloads with the process,
    // which also unmaps the view.
    return TRUE;
}
