// Compare-and-restore for INLINE patches (17_HOOK_ENGINE §Unhooking, §H7) --
// the shape the Overlay's hooks actually are, driven against a REAL MinHook patch
// and a SIMULATED foreign one, deterministically, with no overlay software
// installed and no game.
//
// The existing ctest fl_unhook_preserves_foreign proves the rule for VTABLE
// swaps in the harness. The Overlay swaps no vtable: every hook it installs is a
// MinHook inline patch, and MH_DisableHook writes the saved prologue back without
// looking. So the predicate the Overlay consults before disabling -- "is the jump
// at the target still the one that ends at OUR detour?" -- is proved here, both
// directions, exactly as H7 demanded of the vtable form: it must say YES for our
// own untouched patch (or a compare-and-restore that never restores is not a fix)
// and NO once somebody patched over us.

#include <windows.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <fl_patch_check.h>
#include <MinHook.h>

namespace {

// A real function with a real, relocatable prologue. noinline and non-trivial so
// the compiler keeps a body MinHook can patch, and volatile so nothing folds.
__declspec(noinline) int Target(int x) {
    volatile int acc = x;
    for (int i = 0; i < 3; ++i) {
        acc = acc * 3 + i;
    }
    return acc;
}

using TargetFn = int (*)(int);
TargetFn g_orig = nullptr;

int Detour(int x) {
    return g_orig(x) + 1000;
}

// The foreign overlay's detour and the relay it jumps through, in the exact
// JMP_ABS shape MinHook uses, so the bytes at the target look like a legitimate
// patch that simply ends somewhere else.
int ForeignDetour(int x) {
    return x - 1;
}

// Within +/-2 GB of `near`, as MinHook's own relays are: an E9's rel32 cannot
// reach an arbitrary 64-bit address, and a relay placed anywhere by
// VirtualAlloc(nullptr) is exactly what crashed the first run of this test.
std::uint8_t* AllocNear(const void* anchor) {
    const auto base = reinterpret_cast<std::uintptr_t>(anchor);
    for (std::uintptr_t offset = 0x100000; offset < 0x40000000; offset += 0x10000) {
        for (int dir = 0; dir < 2; ++dir) {
            const std::uintptr_t candidate =
                (dir == 0 ? base - offset : base + offset) & ~static_cast<std::uintptr_t>(0xFFFF);
            void* p = VirtualAlloc(reinterpret_cast<void*>(candidate), 4096, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p != nullptr) {
                return static_cast<std::uint8_t*>(p);
            }
        }
    }
    return nullptr;
}

std::uint8_t* MakeRelay(const void* to, const void* anchor) {
    std::uint8_t* relay = AllocNear(anchor);
    REQUIRE(relay != nullptr);
    relay[0] = 0xFF;
    relay[1] = 0x25;
    std::memset(relay + 2, 0, 4);
    const auto addr = reinterpret_cast<std::uint64_t>(to);
    std::memcpy(relay + 6, &addr, sizeof(addr));
    return relay;
}

// Overwrite the rel32 of the E9 at `target` so it lands on `relay` -- what a
// foreign overlay that patches after us leaves behind.
void RepointJump(void* target, const void* relay, std::int32_t* savedRel) {
    auto* p = static_cast<std::uint8_t*>(target);
    REQUIRE(p[0] == 0xE9);
    std::memcpy(savedRel, p + 1, sizeof(*savedRel));
    const auto rel = static_cast<std::int32_t>(reinterpret_cast<const std::uint8_t*>(relay) - (p + 5));
    DWORD      old = 0;
    REQUIRE(VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old));
    std::memcpy(p + 1, &rel, sizeof(rel));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

void RestoreJump(void* target, std::int32_t savedRel) {
    auto* p = static_cast<std::uint8_t*>(target);
    DWORD old = 0;
    REQUIRE(VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old));
    std::memcpy(p + 1, &savedRel, sizeof(savedRel));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

}    // namespace

TEST_CASE("an inline patch is recognised as ours, then not once a foreign hooker patches over it, then ours again",
          "[unhook][inline]") {
    void* target = reinterpret_cast<void*>(&Target);
    void* detour = reinterpret_cast<void*>(&Detour);

    // Unpatched: nothing is ours, and asking must not fault or claim otherwise.
    CHECK_FALSE(fl::patch::StillOurs(target, detour));

    REQUIRE(MH_Initialize() == MH_OK);
    REQUIRE(MH_CreateHook(target, detour, reinterpret_cast<void**>(&g_orig)) == MH_OK);
    REQUIRE(MH_EnableHook(target) == MH_OK);
    REQUIRE(Target(1) == Detour(1));    // the hook is live (the detour's own call goes through the trampoline)
    CHECK(Target(1) >= 1000);

    // 1. Our own untouched patch IS ours -- the half a compare-and-restore that never restores would fail.
    CHECK(fl::patch::StillOurs(target, detour));
    CHECK_FALSE(fl::patch::StillOurs(target, reinterpret_cast<void*>(&ForeignDetour)));

    // 2. A foreign overlay patches after us: the E9 now lands on THEIR relay. Not ours any more.
    std::uint8_t* foreignRelay = MakeRelay(reinterpret_cast<void*>(&ForeignDetour), target);
    std::int32_t  savedRel = 0;
    RepointJump(target, foreignRelay, &savedRel);
    CHECK(Target(5) == 4);    // their hook is what runs now
    CHECK_FALSE(fl::patch::StillOurs(target, detour));
    // ...and a MinHook disable here would have written our prologue back over them; the
    // Overlay's StopObserving consults the predicate first and leaves the slot alone.

    // 3. Put our jump back (the foreign overlay went away), and it is ours again -- so the
    //    predicate discriminates on the bytes, not on history.
    RestoreJump(target, savedRel);
    CHECK(fl::patch::StillOurs(target, detour));
    CHECK(Target(1) >= 1000);

    // 4. The restore half: with the slot ours, MH_DisableHook restores the original behaviour.
    REQUIRE(MH_DisableHook(target) == MH_OK);
    CHECK(Target(1) == 32);    // ((1*3+0)*3+1)*3+2
    CHECK_FALSE(fl::patch::StillOurs(target, detour));

    MH_Uninitialize();
    VirtualFree(foreignRelay, 0, MEM_RELEASE);
}

TEST_CASE("garbage at the target is 'not ours', never a fault", "[unhook][inline]") {
    // A jump whose rel32 points into unmapped space must read as not-ours, not crash
    // the process asking. Built on our own page rather than on a real function.
    auto* page =
        static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(page != nullptr);
    page[0] = 0xE9;
    const std::int32_t rel = 0x7FFF0000;    // somewhere far away and almost certainly unmapped
    std::memcpy(page + 1, &rel, sizeof(rel));
    CHECK_FALSE(fl::patch::StillOurs(page, reinterpret_cast<void*>(&Detour)));
    CHECK_FALSE(fl::patch::StillOurs(nullptr, reinterpret_cast<void*>(&Detour)));
    CHECK_FALSE(fl::patch::StillOurs(page, nullptr));
    VirtualFree(page, 0, MEM_RELEASE);
}
