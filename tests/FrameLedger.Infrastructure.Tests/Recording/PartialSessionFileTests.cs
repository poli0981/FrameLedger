using FluentAssertions;
using FrameLedger.Application.Recording;
using FrameLedger.Application.Telemetry;
using FrameLedger.Domain.Sessions;
using FrameLedger.Infrastructure.Recording;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Tests.Recording;

/// <summary>
/// <c>14_TESTING</c>: the <c>.partial</c> recovery artifact. Every chunk type round-trips, and a file cut at
/// EVERY byte offset yields exactly the chunks that were complete before the cut — the property the
/// format exists for (<c>06_DATA_MODEL</c> §The <c>.partial</c> file).
/// </summary>
public sealed class PartialSessionFileTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-partial-" + Guid.NewGuid().ToString("N"));

    public PartialSessionFileTests() => Directory.CreateDirectory(_dir);

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

    private static readonly DateTimeOffset _t0 = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    private static PartialHeader Header(Guid guid) => new()
    {
        SessionGuid = guid,
        StartedAt = _t0,
        QpcEpoch = 1_000_000,
        QpcFrequency = 10_000_000,
        GameId = 7,
        SnapshotId = 3,
        ExePath = @"C:\Games\Title\game.exe",
        Pid = 4242,
        Tier = CaptureTier.Hooked,
        Mode = CaptureMode.Launch,
        OverlayBuildId = "abc123-dirty",
        TelemetryDescriptor = "l1+lhm",
        LaunchWaitMs = 812,
    };

    private static FlFrameRecord Record(uint i) => new() { FrameIndex = i, Qpc = 1_000_000 + i * 100_000UL, SwapchainId = 1, MeasuredMask = 0x240 };

    private static TelemetrySample Sample(long qpc, double? temp, double? vram) => new(qpc, new GpuSample
    {
        TakenAt = _t0.AddSeconds(qpc / 10_000_000.0),
        Layer = TelemetryLayer.Lhm,
        TempCoreC = temp,
        VramAdapterMb = vram,
        PcieGen = 4,
    });

    /// <summary>Header, records in two chunks, gaps, sensors, a tick, touches, notes.</summary>
    private string WriteFixture(Guid guid, out FlWriterState state)
    {
        string path = Path.Combine(_dir, "fixture.partial");
        state = new FlWriterState { Status = 1, HooksInstalledMask = 0x3, RuntimeCensus = 0x8001, FaultCount = 0 };
        using IPartialSessionWriter w = PartialSessionFile.Create(path, Header(guid));
        w.AppendNote(_t0, "started Launch");
        w.AppendRecords(0, [Record(0), Record(1), Record(2)]);
        w.AppendGaps([2]);
        w.AppendSensors([Sample(1_100_000, 61.5, 3000), Sample(1_200_000, null, 3001)]);
        w.AppendTouches([1_050_000, 1_150_000]);
        w.AppendTick(new PartialTick(3, 2, 0, 1, 1, _t0.AddSeconds(1).ToUnixTimeMilliseconds(), state));
        w.AppendRecords(3, [Record(3), Record(4)]);
        w.AppendTick(new PartialTick(5, 4, 7, 1, 2, _t0.AddSeconds(2).ToUnixTimeMilliseconds(), state));
        w.AppendNote(_t0.AddSeconds(2), "ended TargetExited");
        return path;
    }

    [Fact]
    public void EveryChunkTypeRoundTrips()
    {
        var guid = Guid.NewGuid();
        string path = WriteFixture(guid, out FlWriterState state);

        PartialSession? p = PartialSessionFile.Read(path);

        p.Should().NotBeNull();
        p!.Truncated.Should().BeFalse();
        p.Header.Should().Be(Header(guid));
        p.Records.Select(r => r.FrameIndex).Should().Equal(0u, 1u, 2u, 3u, 4u);
        p.Records[4].Qpc.Should().Be(Record(4).Qpc);
        p.GapBefore.Should().Equal(2);
        p.Sensors.Should().HaveCount(2);
        p.Sensors[0].QpcTicks.Should().Be(1_100_000);
        p.Sensors[0].Sample.TempCoreC.Should().Be(61.5);
        p.Sensors[0].Sample.PcieGen.Should().Be(4);
        p.Sensors[1].Sample.TempCoreC.Should().BeNull("null stays null across the file: N/A is never 0");
        p.Sensors[1].Sample.VramAdapterMb.Should().Be(3001);
        p.Sensors[1].Sample.LoadPct.Should().BeNull();
        p.TouchQpc.Should().Equal(1_050_000, 1_150_000);
        p.LastTick.Should().NotBeNull();
        p.LastTick!.Value.DrainTicks.Should().Be(5, "the last tick wins");
        p.LastTick.Value.TotalDropped.Should().Be(7);
        p.LastTick.Value.WriterState.RuntimeCensus.Should().Be(state.RuntimeCensus);
        p.Notes.Select(n => n.Text).Should().Equal("started Launch", "ended TargetExited");
        p.Notes[1].At.Should().Be(_t0.AddSeconds(2));
    }

    [Fact]
    public void AKillAtEveryByteOffsetYieldsExactlyTheChunksThatWereComplete()
    {
        var guid = Guid.NewGuid();
        string path = WriteFixture(guid, out _);
        byte[] whole = File.ReadAllBytes(path);
        PartialSession full = PartialSessionFile.Parse(whole)!;

        // Chunk boundaries, from the format itself: type|len|payload|crc.
        var boundaries = new List<int>();
        int at = 0;
        while (at < whole.Length)
        {
            int len = BitConverter.ToInt32(whole, at + 4);
            at += 12 + len;
            boundaries.Add(at);
        }

        boundaries[^1].Should().Be(whole.Length);
        int headerEnd = boundaries[0];

        for (int cut = 0; cut <= whole.Length; cut++)
        {
            PartialSession? p = PartialSessionFile.Parse(whole.AsSpan(0, cut));
            int complete = boundaries.Count(b => b <= cut);
            if (cut < headerEnd)
            {
                p.Should().BeNull($"cut at {cut} is inside the header: nothing to recover");
                continue;
            }

            p.Should().NotBeNull($"cut at {cut}");
            p!.Truncated.Should().Be(cut != whole.Length && boundaries.Contains(cut) == false || (cut != whole.Length && cut < whole.Length && !boundaries.Contains(cut)),
                $"cut at {cut}: mid-chunk is truncated, on a boundary is not");
            // Everything before the cut that was complete is present; nothing after it is.
            int recordsExpected = complete >= 3 ? 3 : 0;
            recordsExpected = complete >= 8 ? 5 : recordsExpected;
            p.Records.Should().HaveCount(recordsExpected, $"cut at {cut}, {complete} complete chunk(s)");
            p.Notes.Should().HaveCount(complete >= 10 ? 2 : complete >= 2 ? 1 : 0, $"cut at {cut}");
            (p.LastTick?.DrainTicks).Should().Be(complete >= 9 ? 5 : complete >= 7 ? 3 : null, $"cut at {cut}");
        }

        full.Truncated.Should().BeFalse();
    }

    [Fact]
    public void ACorruptedByteInsideAChunkDropsThatChunkAndEverythingAfterItAndKeepsWhatCameBefore()
    {
        var guid = Guid.NewGuid();
        string path = WriteFixture(guid, out _);
        byte[] whole = File.ReadAllBytes(path);
        int len0 = BitConverter.ToInt32(whole, 4);
        int chunk1 = 12 + len0;                       // the first note
        int len1 = BitConverter.ToInt32(whole, chunk1 + 4);
        int chunk2 = chunk1 + 12 + len1;              // the first records chunk
        whole[chunk2 + 8 + 5] ^= 0xFF;                // one bit-flip inside its payload

        PartialSession? p = PartialSessionFile.Parse(whole);

        p.Should().NotBeNull();
        p!.Truncated.Should().BeTrue();
        p.Notes.Should().ContainSingle();
        p.Records.Should().BeEmpty("the check on the records chunk failed, and nothing after a failed chunk is trusted");
    }

    [Fact]
    public void ARecordsChunkThatDoesNotContinueTheSequenceIsRefused()
    {
        string path = Path.Combine(_dir, "seq.partial");
        using (IPartialSessionWriter w = PartialSessionFile.Create(path, Header(Guid.NewGuid())))
        {
            w.AppendRecords(0, [Record(0)]);
            w.AppendRecords(5, [Record(5)]);
            w.AppendNote(_t0, "after");
        }

        PartialSession p = PartialSessionFile.Read(path)!;

        p.Records.Should().ContainSingle();
        p.Truncated.Should().BeTrue();
        p.Notes.Should().BeEmpty();
    }

    [Fact]
    public void AFileWithNoHeaderOrAForeignFormatVersionIsNothingToRecover()
    {
        PartialSessionFile.Parse([]).Should().BeNull();
        PartialSessionFile.Parse(new byte[7]).Should().BeNull();

        string path = Path.Combine(_dir, "v99.partial");
        using (IPartialSessionWriter w = PartialSessionFile.Create(path, Header(Guid.NewGuid()) with { FormatVersion = 99 }))
        {
        }

        PartialSessionFile.Read(path).Should().BeNull("a version this reader does not know is not guessed at");
    }

    [Fact]
    public void CreateNeverOverwrites()
    {
        string path = Path.Combine(_dir, "once.partial");
        PartialSessionFile.Create(path, Header(Guid.NewGuid())).Dispose();

        Action again = () => PartialSessionFile.Create(path, Header(Guid.NewGuid())).Dispose();

        again.Should().Throw<IOException>("one guid is one session; a second file with the same name is a bug, not a retry");
    }
}
