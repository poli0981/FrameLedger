using FluentAssertions;
using FrameLedger.Domain.Metrics;
using static FrameLedger.Domain.Tests.Metrics.SampleFixtures;

namespace FrameLedger.Domain.Tests.Metrics;

/// <summary>The install window: a clean suffix, or nothing.</summary>
public sealed class RecordWindowTests
{
    [Fact]
    public void ALazilyInstalledHookYieldsTheSuffixAfterItsFirstClaim()
    {
        List<FrameSample> stream = [.. Stream(5), .. Stream(10, extra: MeasuredFields.Upscaler)];

        RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.Upscaler).Should().Be(5);
    }

    [Fact]
    public void AnIntermittentClaimIsNotAnInstallWindow()
    {
        // hooksInstalledMask is monotonic, so a bit that clears and returns is not a lazily installed hook;
        // a trailing run always looks like a clean suffix on its own, which is why the prefix is checked.
        List<FrameSample> stream = [.. Stream(3, extra: MeasuredFields.Rt), .. Stream(3), .. Stream(3, extra: MeasuredFields.Rt)];

        RecordWindow.ClaimedSuffixStart(stream, MeasuredFields.Rt).Should().Be(stream.Count);
    }

    [Fact]
    public void NoClaimAnywhereIsCount()
    {
        RecordWindow.ClaimedSuffixStart(Stream(4), MeasuredFields.Hdr).Should().Be(4);
        RecordWindow.ClaimedSuffixStart([], MeasuredFields.Hdr).Should().Be(0);
    }

    [Fact]
    public void TheGenericFormTakesAnyPredicate()
    {
        int[] values = [0, 0, 7, 7, 7];

        RecordWindow.ClaimedSuffixStart(values, v => v == 7).Should().Be(2);
    }

    [Fact]
    public void SecondsSumTheIntervalsAndSkipTheOnesTheClockDidNotAdvanceAcross()
    {
        List<FrameSample> stream = Stream(4, step: Frequency / 100);
        // A lapped drain can return an older record after a newer one; that delta is not a frame time.
        stream.Add(Present(stream[0].Qpc));

        RecordWindow.SecondsOf(stream, 0, Frequency).Should().BeApproximately(0.03, 1e-9);
        RecordWindow.SecondsOf(stream, 2, Frequency).Should().BeApproximately(0.01, 1e-9);
        RecordWindow.SecondsOf(stream, 4, Frequency).Should().Be(0);
    }

    [Fact]
    public void AZeroFrequencyIsRefused() =>
        FluentActions.Invoking(() => RecordWindow.SecondsOf(Stream(2), 0, 0)).Should().Throw<ArgumentOutOfRangeException>();
}
