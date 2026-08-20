// The D3D12 command-list vtable slots HANDOFF item 4's ray-tracing hooks patch —
// in ONE place, for the same reason fl_dxgi_vtable.h exists.
//
// WHY A SECOND HEADER RATHER THAN TWO MORE CONSTANTS IN THE FIRST. The DXGI file
// is about IDXGISwapChain and is consumed by a fixture that creates one; these
// are about ID3D12GraphicsCommandList4 and are proved by a fixture that creates a
// D3D12 device. Same rule, different interface, different failure mode.
//
// WHERE THESE NUMBERS COME FROM. The COM ABI fixes a method's slot by the
// declaration order of the whole inheritance chain, so they are derivable rather
// than measured -- and derivable is exactly the kind of confidence this project
// distrusts, which is why ctest fl_d3d12_vtable_indices proves each one by
// BEHAVIOUR. The derivation, so a reader can check the arithmetic:
//
//   IUnknown                     0..2    QueryInterface, AddRef, Release
//   ID3D12Object                 3..6    Get/SetPrivateData, SetPrivateDataInterface, SetName
//   ID3D12DeviceChild            7       GetDevice
//   ID3D12CommandList            8       GetType
//   ID3D12GraphicsCommandList    9..59   Close .. ExecuteIndirect          (51 methods)
//   ID3D12GraphicsCommandList1  60..65   AtomicCopyBufferUINT .. SetViewInstanceMask
//   ID3D12GraphicsCommandList2  66       WriteBufferImmediate
//   ID3D12GraphicsCommandList3  67       SetProtectedResourceSession
//   ID3D12GraphicsCommandList4  68..76   BeginRenderPass, EndRenderPass,
//                                        InitializeMetaCommand, ExecuteMetaCommand,
//                                        BuildRaytracingAccelerationStructure,
//                                        EmitRaytracingAccelerationStructurePostbuildInfo,
//                                        CopyRaytracingAccelerationStructure,
//                                        SetPipelineState1, DispatchRays
//
// WHAT THIS FILE IS NOT. It is not permission to hardcode a vtable POINTER, and
// it is not permission to acquire the vtable from a throwaway WARP device. The
// address is read off a live object; which object is a separate decision, and
// 17_HOOK_ENGINE records that the command-list vtable is taken from a list
// created on the GAME'S OWN device -- an object DXGI handed us for a swapchain we
// were called on -- so a WARP/hardware vtable difference cannot arise. Whether
// such a difference exists at all is what --probe-dxr answers; the answer does
// not change the design, it only says how much the design was buying.
//
// AND THE INDICES ARE STILL NOT TRUSTED. The ctest patches each slot, calls the
// method BY NAME through the interface, and checks the detour ran. An index that
// is merely declared correct in a header is an assumption with a comment on it.

#ifndef FRAMELEDGER_FL_D3D12_VTABLE_H
#define FRAMELEDGER_FL_D3D12_VTABLE_H

namespace fl::d3d12 {

// ID3D12GraphicsCommandList4. Both are recorded into a command list, so they
// count RECORDED rather than EXECUTED work -- 20_OPEN_QUESTIONS §H6, and the unit
// belongs in 03_METRICS beside the numbers derived from it.
inline constexpr unsigned kBuildRaytracingAccelerationStructureIndex = 72;
inline constexpr unsigned kDispatchRaysIndex = 76;

}    // namespace fl::d3d12

#endif    // FRAMELEDGER_FL_D3D12_VTABLE_H
