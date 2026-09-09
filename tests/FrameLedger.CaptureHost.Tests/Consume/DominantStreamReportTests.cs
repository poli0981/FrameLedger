using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The host's side of segmentation: the dominant stream is picked on the shared-memory record with
/// Domain's rule, and the facts are built over THAT stream's intervals.
/// </summary>
public sealed class DominantStreamReportTests
{
    private static FlFrameRecord Present(uint index, uint swapchain, ulong qpc) => new()
    {
        FrameIndex = index,
        SwapchainId = swapchain,
        OutputW = 3840,
        OutputH = 2160,
        Qpc = qpc,
        MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs),
    };

    [Fact]
    public void IntervalsAreTakenWithinTheStreamAndNotFromTheProcessWideFrameIndex()
    {
        // dllmain.cpp assigns `g_frameIndex++` once per accepted present for the WHOLE PROCESS, so within one
        // stream of an interleaved pair consecutive records' indices differ by 2, not 1. An interval rule keyed
        // on `FrameIndex == previous + 1` would exclude EVERY interval here.
        long step = Stopwatch.Frequency / 100;
        List<FlFrameRecord> records = [];
        ulong qpc = 1_000_000;
        for (uint i = 0; i < 100; i++)
        {
            records.Add(Present(i * 2, swapchain: 1, qpc: qpc));
            records.Add(Present((i * 2) + 1, swapchain: 2, qpc: qpc + 1));
            qpc += (ulong)step;
        }

        IReadOnlyList<FlFrameRecord> dominant = SegmentBuilder.DominantStream(records, static r => r.SwapchainId);
        MeasuredFacts facts = MeasuredFacts.From(dominant, default, Stopwatch.Frequency, 0, 0);

        facts.PresentsObserved.Should().Be(100);
        facts.SecondsObserved.Should().BeGreaterThan(0, "a stream whose indices step by 2 still has intervals");
        facts.DisplayedFps.Should().BeApproximately(100, 2);
    }

    [Fact]
    public void AnEmptyRingProducesNothingRatherThanAZeroFps()
    {
        SegmentBuilder.DominantStream<FlFrameRecord>([], static r => r.SwapchainId).Should().BeEmpty();
        MeasuredFacts.From([], default, Stopwatch.Frequency, 0, 0).DisplayedFps.Should().BeNull();
    }
}
