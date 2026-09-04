// A stub that answers to the name `amd_fidelityfx_dx12.dll` -- the SDK 1.1.x
// MONOLITH, which Lies of P, Cyberpunk 2077, Rune Factory and Black Myth: Wukong
// ship (1.0.1.41314) -- and exports the five ffx-api entry points.
//
// A LEAF: the 1.1.x monolith compiled its providers in, so the game's call and the
// effect's dispatch are the same export, and the Overlay hooks it directly. The
// body is shared with the two SDK 2.x leaves; see stub_ffx_leaf.inl.
#define FL_STUB_FFX_MODULE L"amd_fidelityfx_dx12.dll"
#include "stub_ffx_leaf.inl"
