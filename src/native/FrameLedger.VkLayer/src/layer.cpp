// FrameLedger.VkLayer — Vulkan implicit layer.
//
// Scaffold. Vulkan titles use a layer INSTEAD of injection: it is the
// mechanism Khronos supports, and it is how OBS and RTSS do it.
//
// BLOCKED on docs/20_OPEN_QUESTIONS.md §S2 before any interception is added.
// An implicit layer is machine-wide and loads before anything of ours runs, so
// today this path has:
//   - no anti-cheat guard (the guard is defined as pre-injection only, and
//     there is no injection here), and
//   - no mid-session unhook, though docs/19_SAFETY calls that "the single most
//     important runtime behavior in the whole capture layer".
//
// The proposed fix is enable_environment in the manifest, so the loader does
// not map us unless the Agent sets the variable when launching an opted-in
// game — which makes Vulkan Tier 1 launch-mode-only — plus an in-layer module
// scan that goes passthrough on any blocklist hit.
//
// Until then this target builds but intercepts nothing. That is the correct
// failure mode: passthrough.

#include <fl_shm.h>

namespace fl::vklayer {

unsigned int LayoutVersion() noexcept {
    return FL_SHM_LAYOUT_VERSION;
}

}    // namespace fl::vklayer
