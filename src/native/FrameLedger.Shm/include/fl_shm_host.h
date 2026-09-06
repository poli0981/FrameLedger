// The capture side's HOST half of the shared-memory contract: create the one
// ring a process may carry, and publish the handshake a reader validates first.
//
// Header-only and shared by the two capture sides -- the injected Overlay and
// the Vulkan layer -- because 07_IPC says ONE ring per process, named
// Local\FrameLedger.Ring.<pid>, with ONE layout, ONE build id and ONE security
// descriptor, and two copies of that are two places for it to drift. It lived in
// FrameLedger.Overlay/src/dllmain.cpp until 2026-09-06 (P1 item 3), when the
// layer gained vkQueuePresentKHR and needed exactly the same thing.
//
// Not a hook path: this runs once, at init, on whichever thread creates the
// ring. It may allocate (LocalAlloc inside the SDDL conversion) and it may fail
// -- and every failure returns false, so the caller stays inert rather than
// sharing a ring it does not own.
//
// WHO OWNS THE RING WHEN BOTH SIDES ARE IN ONE PROCESS. A Vulkan title launched
// through the host has the layer mapped by the loader and could also receive the
// Overlay if the guard injected it; whichever creates the mapping FIRST owns it,
// and the other sees ERROR_ALREADY_EXISTS here and stays inert. That is the same
// rule the Overlay always had for a pre-existing mapping under its own pid --
// "either a stale object or another writer: refuse rather than share" -- and it
// is why the guard does not inject a Vulkan-only presenter at all
// (fl_guard.cpp, Reason::kTargetIsVulkanLayered).

#ifndef FRAMELEDGER_FL_SHM_HOST_H
#define FRAMELEDGER_FL_SHM_HOST_H

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sddl.h>

#include "fl_shm.h"

#ifndef FL_BUILD_ID
#error "FL_BUILD_ID must be defined (FrameLedger.Shm's INTERFACE compile definition)"
#endif

namespace fl::shmhost {

// The mapping is created with a DACL granting ONLY the current user's SID, and
// lives in the session-scoped Local\ namespace (docs/07_IPC.md §Security). No
// Global\ object: it would need admin and would be visible across sessions for
// no benefit.
//
// Returns false rather than falling back to a default DACL. A mapping the whole
// machine can write is not a degraded version of this one -- the Agent's control
// block is in it, and unhookRequested is the safety stop.
inline bool BuildUserOnlySecurity(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd) noexcept {
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
inline bool MakeRingName(wchar_t* out, std::size_t cap, DWORD pid) noexcept {
    return _snwprintf_s(out, cap, _TRUNCATE, L"Local\\FrameLedger.Ring.%lu", pid) >= 0;
}

// Create and map this process's ring. On success `mapping` and `base` are set;
// on any failure both stay null and nothing is left open.
inline bool CreateRingMapping(HANDLE& mapping, void*& base) noexcept {
    mapping = nullptr;
    base = nullptr;

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

    const std::size_t bytes = fl::FlShmSizeForCapacity(FL_SHM_DEFAULT_CAPACITY);
    HANDLE            h = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, static_cast<DWORD>(bytes >> 32),
                                             static_cast<DWORD>(bytes & 0xFFFFFFFFu), name);
    const DWORD       err = GetLastError();
    LocalFree(sd);
    if (h == nullptr) {
        return false;
    }
    // A pre-existing mapping under our own pid is not a mapping we understand:
    // either a stale object or another writer. Refuse rather than share a ring
    // with an unknown producer (see the file header for the two-capture-sides case).
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return false;
    }

    void* view = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes);
    if (view == nullptr) {
        CloseHandle(h);
        return false;
    }
    mapping = h;
    base = view;
    return true;
}

// Write-once at init, per docs/07_IPC.md §A + B. adapterLuid is deliberately
// left 0 = "not yet known": on the Overlay this runs two steps before any
// graphics module is resolved, and our own dummy device's adapter is not the
// game's (#36); on the layer there is no adapter LUID to publish at all.
inline void PublishHandshake(void* base) noexcept {
    auto* h = reinterpret_cast<FlShmHandshake*>(static_cast<unsigned char*>(base) + FL_SHM_HANDSHAKE_OFFSET);
    h->recordSize = sizeof(FlFrameRecord);
    h->capacity = FL_SHM_DEFAULT_CAPACITY;
    h->pid = GetCurrentProcessId();

    // FL_BUILD_ID comes from git describe at configure time (FrameLedger.Shm's
    // CMakeLists). It had no producer at all until #36; without one the Agent's
    // refuse-to-attach-on-mismatch compares "" with "" forever.
    const char* build = FL_BUILD_ID;
    std::size_t i = 0;
    for (; i + 1 < sizeof(h->buildId) && build[i] != '\0'; ++i) {
        h->buildId[i] = build[i];
    }
    h->buildId[i] = '\0';

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    h->qpcEpoch = static_cast<std::uint64_t>(qpc.QuadPart);

    h->adapterLuid = 0;

    // layoutVersion LAST, with a release fence in front of it. It is the field a
    // reader validates first, so it must not become visible before the fields it
    // vouches for. A reader that saw the version while capacity was still zero
    // would compute a ring of no slots and read garbage.
    std::atomic_thread_fence(std::memory_order_release);
    std::atomic_ref<std::uint32_t> version{h->layoutVersion};
    version.store(FL_SHM_LAYOUT_VERSION, std::memory_order_release);
}

}    // namespace fl::shmhost

#endif    // FRAMELEDGER_FL_SHM_HOST_H
