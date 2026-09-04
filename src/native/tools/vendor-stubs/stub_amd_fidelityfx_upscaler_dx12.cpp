// A stub that answers to the name `amd_fidelityfx_upscaler_dx12.dll` -- the SDK
// 2.x UPSCALER effect DLL (FSR 3.1 and FSR 4 providers; 4.0.2 in Expedition 33,
// Dying Light: The Beast and Kingdom Come: Deliverance II, 4.0.3 in Hell Is Us).
//
// A LEAF, reached either through amd_fidelityfx_loader_dx12.dll's forwarding or
// directly by a UE5 plugin that compiled the loader in. Either way the effect's
// dispatch arrives at THIS module's ffxDispatch, which is why it is a row and the
// loader is not. Body shared with the other leaves; see stub_ffx_leaf.inl.
#define FL_STUB_FFX_MODULE L"amd_fidelityfx_upscaler_dx12.dll"
#include "stub_ffx_leaf.inl"
