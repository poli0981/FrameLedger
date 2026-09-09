using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Infrastructure.Time;

namespace FrameLedger.Infrastructure.Tests.Time;

/// <summary>
/// The managed pipeline stamps with <see cref="Stopwatch.GetTimestamp"/> /
/// <see cref="TimeProvider.GetTimestamp"/> and assumes those ARE <c>QueryPerformanceCounter</c>,
/// the clock the Overlay writes into every record. This pins the assumption to the real counter.
/// </summary>
public sealed class QpcClockTests
{
    [Fact]
    public void StopwatchAndTheSystemTimeProviderAreTheRingsClock()
    {
        QpcClock.Frequency.Should().BePositive();
        Stopwatch.Frequency.Should().Be(QpcClock.Frequency);
        TimeProvider.System.TimestampFrequency.Should().Be(QpcClock.Frequency);

        long qpc = QpcClock.Now();
        long stopwatch = Stopwatch.GetTimestamp();
        long provider = TimeProvider.System.GetTimestamp();

        // Three reads of one counter, taken back to back: within a second of each other by a
        // margin no scheduler hiccup on CI closes, and in order.
        stopwatch.Should().BeGreaterThanOrEqualTo(qpc);
        provider.Should().BeGreaterThanOrEqualTo(stopwatch);
        (provider - qpc).Should().BeLessThan(QpcClock.Frequency, "under one second between the first read and the last");
    }
}
