using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

public sealed class StreamSegmenterTests
{
    private static FlFrameRecord Present(uint index, uint swapchain, ushort w = 3840, ushort h = 2160,
        ulong qpc = 0) => new()
        {
            FrameIndex = index,
            SwapchainId = swapchain,
            OutputW = w,
            OutputH = h,
            Qpc = qpc == 0 ? 1_000_000 + index : qpc,
            MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs),
        };

    [Fact]
    public void StreamsAreSeparatedBySwapchainBeforeResolution()
    {
        // Patching a vtable slot patches the SHARED dxgi.dll class vtable, so one hook sees EVERY
        // swapchain in the process. Two interleaved streams at different sizes are ONE segment each;
        // splitting on resolution first would cut a segment on every alternation.
        List<FlFrameRecord> records = [];
        for (uint i = 0; i < 20; i++)
        {
            records.Add(Present(i * 2, swapchain: 1, w: 3840, h: 2160));
            records.Add(Present((i * 2) + 1, swapchain: 2, w: 512, h: 128));
        }

        IReadOnlyList<Segment> segments = StreamSegmenter.Segment(records);

        segments.Should().HaveCount(2, "two streams, neither of which changed resolution");
        segments.Select(s => s.SwapchainId).Should().BeEquivalentTo([1u, 2u]);
    }

    [Fact]
    public void AResolutionChangeWithinOneStreamSplitsIt()
    {
        // 03_METRICS §Upscaling: "averaging across a settings change is the classic way benchmark
        // numbers become meaningless".
        List<FlFrameRecord> records =
        [
            .. Enumerable.Range(0, 10).Select(i => Present((uint)i, 1, 3840, 2160)),
            .. Enumerable.Range(10, 10).Select(i => Present((uint)i, 1, 2560, 1440)),
        ];

        IReadOnlyList<Segment> segments = StreamSegmenter.Segment(records);

        segments.Should().HaveCount(2);
        segments[0].OutputW.Should().Be(3840);
        segments[1].OutputW.Should().Be(2560);
    }

    [Fact]
    public void AnUnmeasuredSizeDoesNotSplitASegment()
    {
        // FL_MEASURED_OUTPUT_RES clear means the writer had no size to report — the swapchain table
        // overflowed, or GetDesc failed. Treating 0x0 as a resolution change would cut a segment on
        // the writer's silence, which is the affirmative-negative shape layout v3 exists to prevent.
        List<FlFrameRecord> records = [.. Enumerable.Range(0, 10).Select(i => Present((uint)i, 1))];
        FlFrameRecord blind = records[5];
        blind.OutputW = 0;
        blind.OutputH = 0;
        blind.MeasuredMask = (ushort)FlMeasured.PresentArgs;
        records[5] = blind;

        StreamSegmenter.Segment(records).Should().ContainSingle();
    }

    [Fact]
    public void SwapchainIdZeroIsNeverTheDominantStreamWhileARealOneExists()
    {
        // 0 means "the writer could not identify the swapchain", which fl_shm.h says the Agent must
        // treat as one undifferentiated stream and never as a valid id. Reporting "the dominant
        // stream" about records the writer said it could not tell apart is the wrong answer even when
        // there are more of them.
        List<FlFrameRecord> records =
        [
            .. Enumerable.Range(0, 50).Select(i => Present((uint)i, 0)),
            .. Enumerable.Range(50, 5).Select(i => Present((uint)i, 7)),
        ];

        StreamSegmenter.DominantStream(records).Should().OnlyContain(r => r.SwapchainId == 7);
    }

    [Fact]
    public void TheUnidentifiedStreamIsUsedWhenThereIsNothingElse()
    {
        List<FlFrameRecord> records = [.. Enumerable.Range(0, 5).Select(i => Present((uint)i, 0))];

        StreamSegmenter.DominantStream(records).Should().HaveCount(5);
    }

    [Fact]
    public void IntervalsAreTakenWithinTheStreamAndNotFromTheProcessWideFrameIndex()
    {
        // THE DEFECT THIS EXISTS TO CATCH. dllmain.cpp assigns `g_frameIndex++` once per accepted
        // present for the WHOLE PROCESS, four lines before it assigns swapchainId — so within one
        // stream of an interleaved pair, consecutive records' indices differ by 2, not 1. An interval
        // rule keyed on `FrameIndex == previous + 1` would exclude EVERY interval here and report no
        // duration at all, including for Displayed FPS, the one number the consumer may publish.
        long step = Stopwatch.Frequency / 100;
        List<FlFrameRecord> records = [];
        ulong qpc = 1_000_000;
        for (uint i = 0; i < 100; i++)
        {
            records.Add(Present(i * 2, swapchain: 1, qpc: qpc));
            records.Add(Present((i * 2) + 1, swapchain: 2, qpc: qpc + 1));
            qpc += (ulong)step;
        }

        IReadOnlyList<FlFrameRecord> dominant = StreamSegmenter.DominantStream(records);
        MeasuredFacts facts = MeasuredFacts.From(dominant, default, Stopwatch.Frequency, 0, 0);

        facts.PresentsObserved.Should().Be(100);
        facts.SecondsObserved.Should().BeGreaterThan(0, "a stream whose indices step by 2 still has intervals");
        facts.DisplayedFps.Should().BeApproximately(100, 2);
    }

    [Fact]
    public void AnEmptyRingProducesNothingRatherThanAZeroFps()
    {
        StreamSegmenter.DominantStream([]).Should().BeEmpty();
        StreamSegmenter.Segment([]).Should().BeEmpty();
        MeasuredFacts.From([], default, Stopwatch.Frequency, 0, 0).DisplayedFps.Should().BeNull();
    }
}
