using FluentAssertions;
using FrameLedger.Application.Telemetry;
using FrameLedger.Infrastructure.Telemetry;
using LibreHardwareMonitor.Hardware;
using NSubstitute;
using NSubstitute.ExceptionExtensions;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>
/// The fault policy and the capability rules, on a machine with no GPU at all.
/// </summary>
/// <remarks>
/// <c>18_GPU_VENDOR_APIS</c> §Runtime policy: <i>"any layer that throws or hangs twice is
/// disabled for the session and its fields report N/A, rather than being retried in a
/// loop."</i> These drive the source through <see cref="ILhmComputer"/> fakes so every
/// branch of that sentence is reached on CI, where the real library would find nothing.
/// </remarks>
public sealed class LhmTelemetrySourceTests
{
    private sealed class ManualTimeProvider : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = new(2026, 9, 3, 0, 0, 0, TimeSpan.Zero);

        public override DateTimeOffset GetUtcNow() => Now;
    }

    private static ISensor Sensor(SensorType type, string name, float? value)
    {
        ISensor s = Substitute.For<ISensor>();
        s.SensorType.Returns(type);
        s.Name.Returns(name);
        s.Value.Returns(value);
        return s;
    }

    private static IHardware Gpu(params ISensor[] sensors)
    {
        IHardware h = Substitute.For<IHardware>();
        h.HardwareType.Returns(HardwareType.GpuNvidia);
        h.Name.Returns("fake");
        h.Sensors.Returns(sensors);
        h.SubHardware.Returns([]);
        return h;
    }

    private static ILhmComputer Computer(params IHardware[] hardware)
    {
        ILhmComputer c = Substitute.For<ILhmComputer>();
        c.Hardware.Returns(hardware);
        return c;
    }

    private static LhmTelemetrySource Source(ILhmComputer computer, ManualTimeProvider? clock = null) =>
        new(computer, new LhmTelemetryOptions(), clock ?? new ManualTimeProvider());

    [Fact]
    public void APollMapsTheFirstGpuAndPublishesItsFieldsAsCapabilities()
    {
        IHardware cpu = Substitute.For<IHardware>();
        cpu.HardwareType.Returns(HardwareType.Cpu);
        ILhmComputer computer = Computer(cpu, Gpu(Sensor(SensorType.Temperature, "GPU Core", 61f), Sensor(SensorType.Load, "GPU Core", 50f)));
        using LhmTelemetrySource source = Source(computer);

        source.TryRead(out GpuSample? before).Should().BeFalse("nothing has been polled");
        before.Should().BeNull();

        source.PollOnce();

        source.TryRead(out GpuSample? sample).Should().BeTrue();
        sample!.TempCoreC.Should().Be(61);
        sample.LoadPct.Should().Be(50);
        source.Capabilities.Should().Be(GpuCapabilities.TempCore | GpuCapabilities.Load);
        source.Layer.Should().Be(TelemetryLayer.Lhm);
        computer.Received(1).Update();
    }

    [Fact]
    public void ACapabilityIsNeverClaimedForAFieldThatNeverReported()
    {
        // THE §M5 R3 SHAPE: the GPU node is there, every sensor is null. The honest answer is
        // a sample with no fields and Capabilities.None — never a capability over a null.
        using LhmTelemetrySource source = Source(Computer(Gpu(Sensor(SensorType.Temperature, "GPU Core", null))));

        source.PollOnce();

        source.TryRead(out GpuSample? sample).Should().BeTrue("a GPU was found, so there IS a sample — with nothing in it");
        sample!.PresentFields.Should().Be(GpuCapabilities.None);
        source.Capabilities.Should().Be(GpuCapabilities.None);
    }

    [Fact]
    public void CapabilitiesAreMonotonicWhileTheSampleIsNot()
    {
        ISensor temp = Sensor(SensorType.Temperature, "GPU Core", 61f);
        using LhmTelemetrySource source = Source(Computer(Gpu(temp)));

        source.PollOnce();
        temp.Value.Returns((float?)null);
        source.PollOnce();

        source.TryRead(out GpuSample? sample).Should().BeTrue();
        sample!.TempCoreC.Should().BeNull("this tick genuinely has no value");
        source.Capabilities.Should().Be(GpuCapabilities.TempCore, "the field is real on this machine; one null tick does not unlearn that");
    }

    [Fact]
    public void NoGpuNodeIsAnAnswerAndNotAFault()
    {
        IHardware cpu = Substitute.For<IHardware>();
        cpu.HardwareType.Returns(HardwareType.Cpu);
        using LhmTelemetrySource source = Source(Computer(cpu));

        source.PollOnce();

        source.TryRead(out _).Should().BeFalse();
        source.Faults.Should().Be(0);
        source.IsDisabled.Should().BeFalse();
    }

    [Fact]
    public void OneThrowIsToleratedAndTwoDisableTheLayerForGood()
    {
        ISensor temp = Sensor(SensorType.Temperature, "GPU Core", 61f);
        ILhmComputer computer = Computer(Gpu(temp));
        using LhmTelemetrySource source = Source(computer);
        source.PollOnce();
        source.Capabilities.Should().Be(GpuCapabilities.TempCore);

        computer.When(c => c.Update()).Do(_ => throw new InvalidOperationException("driver said no"));

        source.PollOnce();
        source.Faults.Should().Be(1);
        source.IsDisabled.Should().BeFalse("one fault is tolerated");
        source.TryRead(out GpuSample? still).Should().BeTrue("the previous sample is still the latest good one");
        still.Should().NotBeNull();
        source.LastFault.Should().Contain("driver said no");

        source.PollOnce();
        source.Faults.Should().Be(2);
        source.IsDisabled.Should().BeTrue();
        source.Capabilities.Should().Be(GpuCapabilities.None, "a disabled layer reports N/A for everything");
        source.TryRead(out GpuSample? none).Should().BeFalse();
        none.Should().BeNull();

        // NOT RETRIED IN A LOOP. A later poll is a no-op, not a third attempt.
        computer.ClearReceivedCalls();
        source.PollOnce();
        computer.DidNotReceive().Update();
        source.Faults.Should().Be(2);
    }

    [Fact]
    public void AnOpenThatThrowsDisablesAtOnceBecauseThereIsNothingToPoll()
    {
        ILhmComputer computer = Computer();
        computer.When(c => c.Open()).Do(_ => throw new InvalidOperationException("no driver"));
        using LhmTelemetrySource source = Source(computer);

        source.Start();

        source.IsDisabled.Should().BeTrue();
        source.Faults.Should().Be(1, "one fault was counted; the disable is because nothing can be polled, not because two happened");
        source.LastFault.Should().StartWith("Open:");
        computer.DidNotReceive().Update();
    }

    [Fact]
    public void AHungPollIsAFaultCountedOncePerPoll()
    {
        // The reader is the only thread that can notice a hang, because the poller is the thing
        // that is stuck. Block Update() on the poller thread, advance the clock past the
        // threshold, and read.
        using var blocked = new ManualResetEventSlim(false);
        ILhmComputer computer = Computer(Gpu(Sensor(SensorType.Temperature, "GPU Core", 61f)));
        computer.When(c => c.Update()).Do(_ => blocked.Wait(TimeSpan.FromSeconds(30)));
        var clock = new ManualTimeProvider();
        using LhmTelemetrySource source = new(computer, new LhmTelemetryOptions { HangThreshold = TimeSpan.FromSeconds(5) }, clock);

        source.Start();
        SpinWait.SpinUntil(() => source.IsPolling, TimeSpan.FromSeconds(10)).Should().BeTrue("the poller thread must have entered Update()");

        clock.Now += TimeSpan.FromSeconds(1);
        source.TryRead(out _).Should().BeFalse("no sample yet, and one second is not a hang");
        source.Faults.Should().Be(0);

        clock.Now += TimeSpan.FromSeconds(10);
        source.TryRead(out _);
        source.Faults.Should().Be(1, "past the threshold, the poll in progress is one hang");
        source.TryRead(out _);
        source.Faults.Should().Be(1, "the SAME poll is not counted twice");
        source.IsDisabled.Should().BeFalse();

        // Release it, let the next poll hang too, and the second hang disables.
        blocked.Set();
        SpinWait.SpinUntil(() => !source.IsPolling, TimeSpan.FromSeconds(10)).Should().BeTrue();
        blocked.Reset();
        SpinWait.SpinUntil(() => source.IsPolling, TimeSpan.FromSeconds(10)).Should().BeTrue("the loop polls again after its interval");
        clock.Now += TimeSpan.FromSeconds(10);
        source.TryRead(out _).Should().BeFalse();
        source.Faults.Should().Be(2);
        source.IsDisabled.Should().BeTrue();
        source.LastFault.Should().Contain("exceeded");

        blocked.Set();
    }

    [Fact]
    public void DisposeClosesTheLibraryOnlyWhenThePollerCameBack()
    {
        ILhmComputer computer = Computer(Gpu(Sensor(SensorType.Temperature, "GPU Core", 61f)));
        LhmTelemetrySource source = Source(computer);
        source.Start();
        SpinWait.SpinUntil(() => source.TryRead(out _), TimeSpan.FromSeconds(10)).Should().BeTrue();

        source.Dispose();

        computer.Received(1).Open();
        computer.Received(1).Close();
        source.Invoking(s => s.Start()).Should().Throw<ObjectDisposedException>();
    }

    [Fact]
    public void AnIntervalBelowHalfASecondIsRefused()
    {
        Func<LhmTelemetrySource> act = () => new LhmTelemetrySource(Computer(),
            new LhmTelemetryOptions { Interval = TimeSpan.FromMilliseconds(499) }, TimeProvider.System);

        act.Should().Throw<ArgumentOutOfRangeException>();
        new LhmTelemetryOptions().Interval.Should().Be(TimeSpan.FromSeconds(1), "the documented default");
    }
}
