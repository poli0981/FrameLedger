using System.Text;
using FluentAssertions;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Tests.Capture;

/// <summary>The on-disk scan: counts, the chunk boundary, the absent case, and a missing file as an outcome.</summary>
public sealed class ExecutableMarkerScanTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-markers-" + Guid.NewGuid().ToString("N"));

    public ExecutableMarkerScanTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private string Write(string name, byte[] bytes)
    {
        string p = Path.Combine(_dir, name);
        File.WriteAllBytes(p, bytes);
        return p;
    }

    [Fact]
    public void MarkersAreCountedAndTheFgCapableOnesAreNamed()
    {
        byte[] body = Encoding.ASCII.GetBytes("MZ....FidelityFX..ffxFsr3ContextCreate..ffxFsr3ContextDispatchUpscale..NVSDK_NGX_D3D12_Init..end");
        string exe = Write("a.exe", body);

        ExecutableMarkers m = ExecutableMarkerScan.Scan(exe);

        m.Scanned.Should().BeTrue();
        m.BytesScanned.Should().Be(body.Length);
        m.Markers.Single(x => string.Equals(x.Name, "FidelityFX", StringComparison.Ordinal)).Hits.Should().Be(1);
        m.Markers.Single(x => string.Equals(x.Name, "ffxFsr3", StringComparison.Ordinal)).Hits.Should().Be(2);
        m.Markers.Single(x => string.Equals(x.Name, "NVSDK_NGX", StringComparison.Ordinal)).Hits.Should().Be(1);
        m.Markers.Single(x => string.Equals(x.Name, "xefgSwapChain", StringComparison.Ordinal)).Hits.Should().Be(0);
        m.AnyFgCapable.Should().BeTrue("ffxFsr3 belongs to an SDK that generates frames");
        m.FgCapableNames.Should().Be("ffxFsr3");
        m.Describe().Should().Contain("ffxFsr3 x2").And.Contain("can generate frames").And.Contain("NVSDK_NGX x1");
    }

    [Fact]
    public void AMarkerStraddlingTheChunkBoundaryIsCountedExactlyOnce()
    {
        // 64-byte chunks; place "ffxFrameInterpolation" (21 bytes) so it starts at byte 60.
        byte[] body = new byte[200];
        Array.Fill(body, (byte)'.');
        byte[] needle = Encoding.ASCII.GetBytes("ffxFrameInterpolation");
        Buffer.BlockCopy(needle, 0, body, 60, needle.Length);
        Buffer.BlockCopy(needle, 0, body, 150, needle.Length);
        string exe = Write("b.exe", body);

        ExecutableMarkers m = ExecutableMarkerScan.Scan(exe, chunkBytes: 64);

        m.Markers.Single(x => string.Equals(x.Name, "ffxFrameInterpolation", StringComparison.Ordinal)).Hits.Should().Be(2, "one across the boundary, one inside a chunk");
        m.BytesScanned.Should().Be(200);
    }

    [Fact]
    public void AFileWithNoMarkersSaysSoAndAMissingFileIsAnOutcome()
    {
        string clean = Write("c.exe", Encoding.ASCII.GetBytes("MZ nothing vendor-shaped here at all"));

        ExecutableMarkers none = ExecutableMarkerScan.Scan(clean);
        ExecutableMarkers missing = ExecutableMarkerScan.Scan(Path.Combine(_dir, "nope.exe"));

        none.Scanned.Should().BeTrue();
        none.AnyFgCapable.Should().BeFalse();
        none.Describe().Should().Contain("none of 7 marker(s)");
        missing.Scanned.Should().BeFalse();
        missing.Error.Should().NotBeNull();
        missing.Describe().Should().Contain("not scanned");
        ExecutableMarkers.NotScanned.Describe().Should().EndWith("not scanned");
    }
}
