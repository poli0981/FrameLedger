using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The 1 Hz thread's contract: what a tick queues, what a drain returns, what a full queue does.
/// The thread itself is exercised once; every other case drives <see cref="TelemetryPoller.PollOnce"/>.
/// </summary>
public sealed class TelemetryPollerTests
{
    private static readonly DateTimeOffset _t0 = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    private sealed class ManualTimeProvider : TimeProvider
    {
        public long Ticks { get; set; } = 1_000_000;

        public override DateTimeOffset GetUtcNow() => _t0;

        public override long GetTimestamp() => Ticks;
    }

    [Fact]
    public void ATickStampsTheLayersSampleWithTheCallersTimestampClock()
    {
        var clock = new ManualTimeProvider { Ticks = 42_000 };
        using var layer = new FakeLayer(TelemetryLayer.Lhm);
        layer.Publish(layer.Sample(_t0, load: 50));
        using var poller = new TelemetryPoller(layer, new TelemetryPollerOptions(), clock);

        poller.PollOnce();
        clock.Ticks = 52_000;
        poller.PollOnce();

        var drained = new List<TelemetrySample>();
        poller.Drain(drained).Should().Be(2);
        drained.Select(s => s.QpcTicks).Should().Equal(42_000, 52_000);
        drained.Should().AllSatisfy(s => s.Sample.LoadPct.Should().Be(50), "every tick is queued, duplicates included");
        poller.Queued.Should().Be(0);
        poller.Drain(drained).Should().Be(0);
    }

    [Fact]
    public void ALayerWithNothingToSayQueuesNothing()
    {
        using var layer = new FakeLayer(TelemetryLayer.Baseline).Publish(null);
        using var poller = new TelemetryPoller(layer, new TelemetryPollerOptions(), new ManualTimeProvider());

        poller.PollOnce();

        poller.Queued.Should().Be(0);
        layer.Reads.Should().Be(1);
    }

    [Fact]
    public void AFullQueueDropsTheOldestAndCountsIt()
    {
        var clock = new ManualTimeProvider();
        using var layer = new FakeLayer(TelemetryLayer.Lhm);
        layer.Publish(layer.Sample(_t0, load: 1));
        using var poller = new TelemetryPoller(layer, new TelemetryPollerOptions { QueueCapacity = 3 }, clock);

        for (int i = 1; i <= 5; i++)
        {
            clock.Ticks = i;
            poller.PollOnce();
        }

        var drained = new List<TelemetrySample>();
        poller.Drain(drained);
        // The newest survive; a stalled consumer loses the oldest, like the ring.
        drained.Select(s => s.QpcTicks).Should().Equal(3L, 4L, 5L);
        poller.Dropped.Should().Be(2);
        poller.Queued.Should().Be(0);
    }

    [Fact]
    public void TheDescriptorIsTheCompositesOrTheSingleLayersName()
    {
        using var l1 = new FakeLayer(TelemetryLayer.Baseline);
        using var l2 = new FakeLayer(TelemetryLayer.Lhm);
        using var single = new TelemetryPoller(l2, new TelemetryPollerOptions(), new ManualTimeProvider());
        using var merged = new CompositeTelemetrySource([l1, l2]);
        using var composite = new TelemetryPoller(merged, new TelemetryPollerOptions(), new ManualTimeProvider());

        single.Descriptor.Should().Be("lhm");
        composite.Descriptor.Should().Be("l1+lhm");

        l2.IsDisabled = true;
        single.Descriptor.Should().BeEmpty();
        composite.Descriptor.Should().Be("l1");
    }

    [Fact]
    public void FasterThanTheLayersAnswerIsRefused()
    {
        using var layer = new FakeLayer(TelemetryLayer.Lhm);
        Action fast = () => new TelemetryPoller(layer,
            new TelemetryPollerOptions { Interval = TimeSpan.FromMilliseconds(100) }, new ManualTimeProvider()).Dispose();
        Action empty = () => new TelemetryPoller(layer,
            new TelemetryPollerOptions { QueueCapacity = 0 }, new ManualTimeProvider()).Dispose();

        fast.Should().Throw<ArgumentOutOfRangeException>();
        empty.Should().Throw<ArgumentOutOfRangeException>();
    }

    [Fact]
    public void TheThreadPollsAtItsIntervalAndReturnsOnDispose()
    {
        using var layer = new FakeLayer(TelemetryLayer.Lhm);
        layer.Publish(layer.Sample(_t0, load: 1));
        var poller = new TelemetryPoller(layer, new TelemetryPollerOptions { Interval = TelemetryPollerOptions.MinimumInterval }, TimeProvider.System);

        poller.Start();
        poller.Start();
        SpinWait.SpinUntil(() => poller.Queued >= 2, TimeSpan.FromSeconds(10)).Should().BeTrue("500 ms cadence, two ticks well inside 10 s");
        poller.Dispose();

        int after = layer.Reads;
        Thread.Sleep(TelemetryPollerOptions.MinimumInterval * 2);
        layer.Reads.Should().Be(after, "the thread stopped reading");
        layer.Disposed.Should().BeFalse("the poller does not own the source");
    }
}
