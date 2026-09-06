// Compare-and-restore for INLINE patches (17_HOOK_ENGINE §Unhooking, §H7).
//
// The Overlay's hooks are MinHook inline patches on the first bytes of a
// function, not vtable-entry swaps, and the H7 hazard exists for them in the same
// shape: if another overlay patched the same prologue AFTER us, it saved OUR
// jump as its "original" and chains through it. MH_DisableHook writes our saved
// prologue back unconditionally -- which would remove THEIR hook silently, with
// no error anywhere, and we would have caused it. So before disabling a patch,
// the Overlay asks whether the bytes at the target are still the jump MinHook
// wrote for us; if not, the patch is left alone and our detour stays in their
// chain doing nothing (g_observing is 0, every body forwards).
//
// What MinHook writes on x64 (hook.c EnableHookLL / CreateHook):
//   target:      E9 rel32              -> relay
//   relay:       FF 25 00 00 00 00     JMP [RIP+0]
//                <8-byte absolute>     = our detour
// and, when the function has a hot-patch area (patchAbove):
//   target-5:    E9 rel32              -> relay
//   target:      EB F9                 JMP -7 (back to target-5)
// A foreign overlay that patches over us replaces the E9's rel32 (or the whole
// prologue) so the chain no longer ends at our detour -- which is exactly what
// this reads. Reads only; the relay address comes from bytes somebody else may
// have written, so the reads are SEH-guarded and a fault reads as "not ours".
//
// Header-only and MinHook-free so ctest fl_unhook_inline can drive it against a
// real MinHook patch and a simulated foreign one without loading the Overlay.

#ifndef FRAMELEDGER_FL_PATCH_CHECK_H
#define FRAMELEDGER_FL_PATCH_CHECK_H

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace fl::patch {

namespace detail {

inline bool ReadJumpChainEndsAt(const void* target, const void* detour) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(target);
    // patchAbove: a short jump back over the 5-byte jump placed in the hot-patch area.
    if (p[0] == 0xEB && p[1] == 0xF9) {
        p -= 5;
    }
    if (p[0] != 0xE9) {
        return false;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, sizeof(rel));
    const auto* relay = p + 5 + rel;
    if (relay[0] != 0xFF || relay[1] != 0x25) {
        return false;
    }
    std::uint32_t disp = 0;
    std::memcpy(&disp, relay + 2, sizeof(disp));
    if (disp != 0u) {
        return false;
    }
    std::uint64_t address = 0;
    std::memcpy(&address, relay + 6, sizeof(address));
    return address == reinterpret_cast<std::uint64_t>(detour);
}

}    // namespace detail

// True when the patch at `target` is still the one that ends at OUR `detour`.
// False for a foreign patch on top of ours, an unpatched target, or bytes that
// could not be read -- every uncertainty is "leave it alone".
inline bool StillOurs(const void* target, const void* detour) noexcept {
    if (target == nullptr || detour == nullptr) {
        return false;
    }
    __try {
        return detail::ReadJumpChainEndsAt(target, detour);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}    // namespace fl::patch

#endif    // FRAMELEDGER_FL_PATCH_CHECK_H
