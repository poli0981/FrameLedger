using System.Globalization;
using FrameLedger.Application.Telemetry;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dxgi;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// The real <see cref="IDxgiAdapters"/>: <c>CreateDXGIFactory1</c> →
/// <c>IDXGIFactory6::EnumAdapterByGpuPreference</c> → <c>IDXGIAdapter3::GetDesc1</c> +
/// <c>CheckInterfaceSupport</c> per adapter, released before returning
/// (<c>18_GPU_VENDOR_APIS</c> §L1).
/// </summary>
/// <remarks>
/// <para>
/// <b>The first CsWin32 consumer in the tree.</b> Marshaling is off, so every interface here is
/// a raw vtable struct and every object a pointer this class releases itself. That is a
/// deliberate trade: an RCW would be bound to the apartment of the thread that created it,
/// and the poller thread, the session loop and a test's thread are not promised to share one.
/// DXGI factories are free-threaded; the pointer form simply lets that be true.
/// </para>
/// <para>
/// <b>Identity only, and that is a measurement.</b> The doc listed
/// <c>IDXGIAdapter3::QueryVideoMemoryInfo</c> as L1's adapter-wide usage; its
/// <c>CurrentUsage</c> is the <i>calling process's</i> usage by the structure's own definition —
/// which is exactly why the Overlay reads it inside the game for <c>vram_proc</c> — and from the
/// Agent it read 0 bytes beside a 16 GB adapter (2026-09-09, RTX 5080). Adapter-wide usage is
/// PDH's (<see cref="PdhAdapterMemoryCounter"/>), as the same section always said.
/// </para>
/// <para>
/// The driver version comes from <c>IDXGIAdapter::CheckInterfaceSupport(IDXGIDevice)</c>, whose
/// documented second output is the user-mode driver version for that interface — the four
/// 16-bit fields of one 64-bit value (<c>32.0.16.1664</c> on the dev box; the WARP adapter answers
/// with the OS build). It is not SetupAPI and not the registry, which the doc listed as candidates;
/// both need a device-instance walk this class has no other reason to carry. A failed query leaves
/// <see cref="GpuAdapterIdentity.DriverVersion"/> null.
/// </para>
/// </remarks>
public sealed class DxgiAdapters : IDxgiAdapters
{
    private const int _dxgiErrorNotFound = unchecked((int)0x887A0002);

    public unsafe IReadOnlyList<GpuAdapterIdentity> Enumerate()
    {
        IDXGIFactory6* factory = null;
        Guid factoryIid = IDXGIFactory6.IID_Guid;
        _ = PInvoke.CreateDXGIFactory1(&factoryIid, (void**)&factory).ThrowOnFailure();

        var adapters = new List<GpuAdapterIdentity>();
        try
        {
            Guid adapterIid = IDXGIAdapter3.IID_Guid;
            for (uint i = 0; ; i++)
            {
                IDXGIAdapter3* adapter = null;
                HRESULT hr = factory->EnumAdapterByGpuPreference(
                    i, DXGI_GPU_PREFERENCE.DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &adapterIid, (void**)&adapter);
                if (hr.Value == _dxgiErrorNotFound)
                {
                    break;
                }

                _ = hr.ThrowOnFailure();
                try
                {
                    adapters.Add(Describe(adapter));
                }
                finally
                {
                    adapter->Release();
                }
            }
        }
        finally
        {
            factory->Release();
        }

        return adapters;
    }

    private static unsafe GpuAdapterIdentity Describe(IDXGIAdapter3* adapter)
    {
        DXGI_ADAPTER_DESC1 desc;
        _ = adapter->GetDesc1(&desc).ThrowOnFailure();

        string? driver = null;
        Guid deviceIid = IDXGIDevice.IID_Guid;
        long umd;
        if (adapter->CheckInterfaceSupport(&deviceIid, &umd).Succeeded)
        {
            ulong v = (ulong)umd;
            driver = string.Create(CultureInfo.InvariantCulture,
                $"{(v >> 48) & 0xFFFF}.{(v >> 32) & 0xFFFF}.{(v >> 16) & 0xFFFF}.{v & 0xFFFF}");
        }

        return new GpuAdapterIdentity
        {
            Name = desc.Description.ToString(),
            Luid = ((ulong)(uint)desc.AdapterLuid.HighPart << 32) | desc.AdapterLuid.LowPart,
            VendorId = desc.VendorId,
            DeviceId = desc.DeviceId,
            SubSysId = desc.SubSysId,
            Revision = desc.Revision,
            DedicatedVideoMemoryMb = desc.DedicatedVideoMemory / 1048576.0,
            SharedSystemMemoryMb = desc.SharedSystemMemory / 1048576.0,
            IsSoftware = desc.Flags.HasFlag(DXGI_ADAPTER_FLAG.DXGI_ADAPTER_FLAG_SOFTWARE),
            DriverVersion = driver,
        };
    }
}
