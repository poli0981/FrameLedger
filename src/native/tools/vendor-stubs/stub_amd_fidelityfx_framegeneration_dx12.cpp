// A stub that answers to the name `amd_fidelityfx_framegeneration_dx12.dll` -- the
// SDK 2.x FRAME-GENERATION effect DLL (FSR 3.1 FG 3.1.5 in Expedition 33 and Dying
// Light: The Beast, FSR 4 FG 4.0.0 in Hell Is Us), which also owns the
// frame-generation swapchain.
//
// A LEAF, for the same reason as the upscaler leaf. In the real module the
// PREPARE dispatch comes from the game once per application frame and the
// FRAMEGENERATION dispatch comes back through the game's own callback once per
// generated batch; this stub counts both by type and generates nothing. Body
// shared with the other leaves; see stub_ffx_leaf.inl.
#define FL_STUB_FFX_MODULE L"amd_fidelityfx_framegeneration_dx12.dll"
#include "stub_ffx_leaf.inl"
