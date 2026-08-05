// The DXGI swapchain vtable slots the Overlay hooks — in ONE place.
//
// WHY THIS FILE EXISTS. These three numbers were written twice: inline in
// FrameLedger.Overlay's MH_CreateHook calls, and again as harness-local
// constants in tools/hook-harness. `ctest fl_vtable_indices` exists to prove
// them by BEHAVIOUR rather than by trusting a header — and it was proving them
// against the harness's own copy. Change the Overlay's 8 to a 9 and that test
// still passed: it established a fact about `dxgi.dll`, not a fact about
// FrameLedger.Overlay. The only test that coupled the two is the drain
// integration class, which CI skips for §S19(b) — so in the merge gate the
// coupling was absent entirely (20_OPEN_QUESTIONS §S29(b)).
//
// One header, two consumers, and the proof now lands on the shipped value.
//
// WHAT THIS FILE IS NOT. It is not permission to hardcode a vtable POINTER.
// 17_HOOK_ENGINE §Getting vtable addresses is unchanged: the Overlay creates a
// throwaway WARP composition swapchain, reads the vtable off that live object,
// and releases it immediately. What is constant across processes and driver
// versions is the SLOT INDEX — fixed by the COM interface's declaration order,
// which is ABI and cannot change without a new interface — not the address.
//
// AND THE INDICES ARE STILL NOT TRUSTED. `hook-harness --probe-vtable` calls
// each slot on a real swapchain and checks it does what its name says, because
// an index that is merely *declared* correct in a header is an assumption with
// a comment on it. This header is where the assumption is written down once;
// the ctest is what makes it a measurement.

#ifndef FRAMELEDGER_FL_DXGI_VTABLE_H
#define FRAMELEDGER_FL_DXGI_VTABLE_H

namespace fl::dxgi {

// IUnknown (0-2) + IDXGIObject (3-6) + IDXGIDeviceSubObject (7) puts
// IDXGISwapChain::Present at 8. IDXGISwapChain runs 8..17, IDXGISwapChain1
// continues at 18, and Present1 is its fifth method.
inline constexpr unsigned kPresentIndex = 8;
inline constexpr unsigned kResizeBuffersIndex = 13;
inline constexpr unsigned kPresent1Index = 22;

}    // namespace fl::dxgi

#endif    // FRAMELEDGER_FL_DXGI_VTABLE_H
