using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>An <see cref="IDxgiAdapters"/> made of hand-built identities, for a machine with no GPU.</summary>
internal sealed class FakeDxgi : IDxgiAdapters
{
    private readonly Func<IReadOnlyList<GpuAdapterIdentity>> _enumerate;

    public FakeDxgi(params GpuAdapterIdentity[] adapters) => _enumerate = () => adapters;

    public FakeDxgi(Func<IReadOnlyList<GpuAdapterIdentity>> enumerate) => _enumerate = enumerate;

    public int Enumerations { get; private set; }

    public IReadOnlyList<GpuAdapterIdentity> Enumerate()
    {
        Enumerations++;
        return _enumerate();
    }

    public static GpuAdapterIdentity Identity(string name, ulong luid, bool software = false) => new()
    {
        Name = name,
        Luid = luid,
        VendorId = 0x10DE,
        DeviceId = 0x2C02,
        SubSysId = 0,
        Revision = 0xA1,
        DedicatedVideoMemoryMb = 15_979,
        SharedSystemMemoryMb = 16_292.97,
        IsSoftware = software,
        DriverVersion = "32.0.16.1664",
    };
}
