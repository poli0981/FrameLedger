using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>The in-process probe maps the bridge's three branches to the outcomes the spawned probe used to print.</summary>
public sealed class NvapiNgxStateProbeTests
{
    [Fact]
    public void AnAnsweredWordIsTheDriverStateVerbatim()
    {
        using var bridge = new FakeNvapiBridge
        {
            Ngx = _ => new NvapiNgxWords { Status = NvapiNgxWords.Answered, Sr = 0x600, Rr = 0x1, Fg = 0x605, Driver = 61664, FrameGenerationCount = 0, ScalingRatio = 0 },
        };
        using var probe = new NvapiNgxStateProbe(bridge);

        NgxDriverState s = probe.Run(4242);

        s.Outcome.Should().Be(NgxProbeOutcome.Answered);
        s.SrCreatedAndEvaluated.Should().BeTrue();
        s.FgCreatedAndEvaluated.Should().BeTrue();
        s.Driver.Should().Be(61664u);
        s.Readings.Should().Be(1);
        s.Answered.Should().Be(1);
    }

    [Fact]
    public void UnansweredAndDegradedAreOutcomesNotThrows()
    {
        using var b1 = new FakeNvapiBridge();
        using var b2 = new FakeNvapiBridge { InitResult = -6 };
        using var b3 = new FakeNvapiBridge { InitResult = NativeNvapiBridge.Unavailable };
        using var b4 = new FakeNvapiBridge { InitResult = NativeNvapiBridge.NoGpu };
        using var unanswered = new NvapiNgxStateProbe(b1);
        using var noDriver = new NvapiNgxStateProbe(b2);
        using var noDll = new NvapiNgxStateProbe(b3);
        using var noGpu = new NvapiNgxStateProbe(b4);

        unanswered.Run(1).Outcome.Should().Be(NgxProbeOutcome.Unanswered);
        unanswered.Run(1).Detail.Should().Contain("-121");
        noDriver.Run(1).Outcome.Should().Be(NgxProbeOutcome.Degraded);
        noDll.Run(1).Detail.Should().Contain("not beside this binary");
        noGpu.Run(1).Outcome.Should().Be(NgxProbeOutcome.Unanswered, "NGX words are per driver; no GPU handle is needed");
    }

    [Fact]
    public void TheBridgeIsInitialisedOnceAndShutDownOnDispose()
    {
        using var bridge = new FakeNvapiBridge();
        var probe = new NvapiNgxStateProbe(bridge);

        probe.Run(1);
        probe.Run(2);
        probe.Dispose();

        bridge.Inits.Should().Be(1);
        bridge.Shutdowns.Should().Be(1);
        bridge.Disposed.Should().BeTrue();
    }
}
