using FluentAssertions;
using FrameLedger.Application.Persistence;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Recording;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Recording;

public sealed class HardwareSnapshotSourceTests
{
    private sealed class OneAdapter : IDxgiAdapters
    {
        public IReadOnlyList<GpuAdapterIdentity> Enumerate() =>
        [
            new()
            {
                Name = "Microsoft Basic Render Driver", Luid = 1, VendorId = 0x1414, DeviceId = 0x8C, SubSysId = 0, Revision = 0,
                DedicatedVideoMemoryMb = 0, SharedSystemMemoryMb = 1, IsSoftware = true, DriverVersion = "10.0.1.1",
            },
            new()
            {
                Name = "NVIDIA GeForce RTX 5080", Luid = 2, VendorId = 0x10DE, DeviceId = 0x2C02, SubSysId = 0, Revision = 0xA1,
                DedicatedVideoMemoryMb = 15_979, SharedSystemMemoryMb = 16_292, IsSoftware = false, DriverVersion = "32.0.16.1664",
            },
        ];
    }

    private sealed class NoFactory : IDxgiAdapters
    {
        public IReadOnlyList<GpuAdapterIdentity> Enumerate() => throw new InvalidOperationException("no DXGI here");
    }

    [Fact]
    public void TakesTheFirstHardwareAdapterAndTheMachinesOwnFacts()
    {
        HardwareSnapshot s = new HardwareSnapshotSource(new OneAdapter()).Take();

        s.GpuName.Should().Be("NVIDIA GeForce RTX 5080", "a software adapter is never the machine's GPU");
        s.GpuDriver.Should().Be("32.0.16.1664");
        s.OsBuild.Should().NotBeNullOrWhiteSpace();
        s.RamGb.Should().BeGreaterThan(0);
        s.Hash.Should().HaveLength(64);
        s.DisplayRes.Should().BeNull("not taken in P2");
    }

    [Fact]
    public void NoFactoryIsASnapshotWithNoGpuNotAFailedSession()
    {
        HardwareSnapshot s = new HardwareSnapshotSource(new NoFactory()).Take();

        s.GpuName.Should().BeNull();
        s.GpuDriver.Should().BeNull();
        s.OsBuild.Should().NotBeNullOrWhiteSpace();
    }
}
