using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The stall diagnostic: the longest intervals, placed against the host's own touches on the target.
/// </summary>
public sealed class StallReportTests
{
    private static readonly long _hz = Stopwatch.Frequency;

    /// <summary>A 120 FPS stream with one gap of <paramref name="gapMs"/> after <paramref name="gapAfter"/> presents.</summary>
    private static List<FlFrameRecord> StreamWithGap(int presents, int gapAfter, double gapMs, out long gapStartQpc)
    {
        List<FlFrameRecord> s = [];
        long qpc = 5_000_000;
        long step = _hz / 120;
        gapStartQpc = 0;
        for (int i = 0; i < presents; i++)
        {
            s.Add(new FlFrameRecord { FrameIndex = (uint)i, Qpc = (ulong)qpc, SwapchainId = 1 });
            if (i == gapAfter)
            {
                gapStartQpc = qpc;
                qpc += (long)(gapMs / 1000.0 * _hz);
            }
            else
            {
                qpc += step;
            }
        }

        return s;
    }

    [Fact]
    public void TheLongestIntervalIsFoundAndPlacedInTheSession()
    {
        List<FlFrameRecord> s = StreamWithGap(600, 240, 1000.0, out _);

        StallReport r = StallReport.From(s, _hz, []);

        r.Intervals.Should().Be(599);
        r.OverThreshold.Should().Be(1);
        r.Longest.Should().HaveCount(3);
        r.Longest[0].Milliseconds.Should().BeApproximately(1000.0, 1.0);
        r.Longest[0].AtSeconds.Should().BeApproximately(2.0, 0.01, "240 presents at 120 FPS");
        r.Longest[0].NearestTouchSeconds.Should().BeNull();
        r.Describe().Should().Contain("1 interval(s) of 599 at or over 100 ms").And.Contain("longest: 1000 ms at t=2.00 s")
            .And.Contain("no host touch recorded");
    }

    [Fact]
    public void AHostTouchInsideTheStallNamesFrameLedgerAsASuspect()
    {
        List<FlFrameRecord> s = StreamWithGap(600, 240, 1000.0, out long gapStart);
        long touch = gapStart + _hz / 2;    // half a second into the gap

        StallReport r = StallReport.From(s, _hz, [touch]);

        r.Longest[0].NearestTouchSeconds.Should().Be(0.0);
        r.Longest[0].NearATouch.Should().BeTrue();
        r.Describe().Should().Contain("A HOST TOUCH INSIDE the interval").And.Contain("SUSPECT");
    }

    [Fact]
    public void AHostTouchJustBeforeTheStallIsStillASuspectAndAFarOneIsNot()
    {
        List<FlFrameRecord> s = StreamWithGap(600, 240, 1000.0, out long gapStart);

        StallReport near = StallReport.From(s, _hz, [gapStart - _hz / 4]);    // 0.25 s before
        StallReport far = StallReport.From(s, _hz, [gapStart + 30 * _hz]);    // 30 s after

        near.Longest[0].NearestTouchSeconds.Should().BeApproximately(-0.25, 0.01);
        near.Longest[0].NearATouch.Should().BeTrue();
        near.Describe().Should().Contain("0.25 s before it");
        far.Longest[0].NearATouch.Should().BeFalse();
        far.Describe().Should().Contain("not ours");
    }

    [Fact]
    public void ASmoothStreamHasNoStallAndStillPrintsItsLongest()
    {
        List<FlFrameRecord> s = StreamWithGap(300, -1, 0, out _);

        StallReport r = StallReport.From(s, _hz, [(long)s[10].Qpc]);

        r.OverThreshold.Should().Be(0);
        r.Longest[0].Milliseconds.Should().BeApproximately(1000.0 / 120, 0.05);
        r.Touches.Should().Be(1);
        StallReport.From([s[0]], _hz, []).Describe().Should().Contain("fewer than two presents");
    }
}
