using FrameLedger.Application.Persistence;
using FrameLedger.Application.Recording;
using FrameLedger.Application.Telemetry;
using FrameLedger.Domain.Sessions;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Recording;

/// <summary>Synthetic sessions the recording tests share: a steady 100 fps stream with the claims a test asks for.</summary>
internal static class SessionFixtures
{
    public const long QpcFrequency = 10_000_000;

    public const ulong QpcEpoch = 5_000_000_000;

    public static readonly DateTimeOffset StartedAt = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    public static SessionRow Skeleton(Guid? guid = null, double seconds = 60, CaptureTier tier = CaptureTier.Hooked) => new()
    {
        SessionGuid = guid ?? Guid.NewGuid(),
        GameId = 7,
        SnapshotId = 3,
        StartedAt = StartedAt,
        EndedAt = StartedAt.AddSeconds(seconds),
        QpcEpoch = QpcEpoch,
        QpcFrequency = QpcFrequency,
        Tier = tier,
        Mode = CaptureMode.Attach,
        ExitStatus = ExitStatus.Normal,
        TelemetrySource = "l1+lhm",
    };

    /// <summary>
    /// <paramref name="count"/> presents 10 ms apart on swapchain 1, every one claiming what
    /// <paramref name="claims"/> says. <paramref name="fgPerBatch"/> &gt; 1 makes one present in N carry an
    /// evaluation (frame generation at that factor); 1 makes every present carry one (<c>none</c>).
    /// </summary>
    public static List<FlFrameRecord> Stream(int count, FlMeasured claims, int fgPerBatch = 1, FlApi api = FlApi.D3D12,
        ulong intervalQpc = 100_000, FlUpscaler upscaler = FlUpscaler.NotReported)
    {
        var records = new List<FlFrameRecord>(count);
        for (int i = 0; i < count; i++)
        {
            records.Add(new FlFrameRecord
            {
                FrameIndex = (uint)i,
                Qpc = QpcEpoch + 10_000_000 + (ulong)i * intervalQpc,
                SwapchainId = 1,
                Api = (byte)api,
                MeasuredMask = (ushort)claims,
                FgEvaluations = (byte)(claims.HasFlag(FlMeasured.FgCounts) && i % fgPerBatch == 0 ? 1 : 0),
                Upscaler = (byte)upscaler,
                RenderW = 1707,
                RenderH = 960,
                OutputW = 2560,
                OutputH = 1440,
                VramUsedMb = (uint)(4000 + i % 10),
                ReflexLatencyUs = (uint)(20_000 + i % 100),
                RtFlags = (byte)(claims.HasFlag(FlMeasured.Rt) && i % 2 == 0 ? FlRtFlags.DispatchObserved : FlRtFlags.None),
                DispatchRaysVolume = (uint)(claims.HasFlag(FlMeasured.Rt) && i % 2 == 0 ? 8_294_400 : 0),
                PsoCreatedThisFrame = (ushort)(i == 5 ? 3 : 0),
            });
        }

        return records;
    }

    public static AggregationInput Hooked(IReadOnlyList<FlFrameRecord> records, FlWriterState? writer = null, IReadOnlyList<int>? gaps = null) => new()
    {
        Records = records,
        GapBefore = gaps ?? [],
        Writer = writer ?? new FlWriterState { Status = 1, HooksInstalledMask = 0x1, RuntimeCensus = 0x8000_0000 },
        QpcFrequency = QpcFrequency,
    };

    public static List<TelemetrySample> Sensors(int seconds, double? temp = 60, double? load = 50)
    {
        var list = new List<TelemetrySample>(seconds);
        for (int i = 0; i < seconds; i++)
        {
            list.Add(new TelemetrySample((long)QpcEpoch + i * QpcFrequency, new GpuSample
            {
                TakenAt = StartedAt.AddSeconds(i),
                Layer = TelemetryLayer.Lhm,
                TempCoreC = temp is { } t ? t + i : null,
                LoadPct = load,
                VramAdapterMb = 3000 + i,
            }));
        }

        return list;
    }
}
