// The smallest raytracing shader that lets DispatchRays be RECORDED.
//
// WHY IT EXISTS. Measured 2026-08-20: ID3D12GraphicsCommandList4::DispatchRays
// ACCESS-VIOLATES at record time when no raytracing state object is bound --
// with a well-formed shader table and with a zeroed one alike, so it is the state
// object and not the descriptor. The D3D12 runtime dereferences the bound state
// object while recording, before anything reaches the GPU. So a fixture that
// exercises the Overlay's DispatchRays detour needs a real RT pipeline, and a
// real RT pipeline needs a DXIL library.
//
// It does nothing on purpose. The fixture records the command list and never
// calls ExecuteCommandLists, so this code never runs anywhere; what it provides
// is a valid state object for the recording path to read.
//
// COMPILED BY HAND AND CHECKED IN as dxr_raygen_dxil.h, deliberately NOT built by
// CMake: dxc.exe ships with the Windows SDK rather than with the C++ toolchain,
// and making the native build depend on it would trade a reproducible 3 KB blob
// for a build that fails on any machine whose SDK layout differs. The exact
// command line is in the generated header so the blob can be regenerated and
// compared.

[shader("raygeneration")] void RayGen() {}
