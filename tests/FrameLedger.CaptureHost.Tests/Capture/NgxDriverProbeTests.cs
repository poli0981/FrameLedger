using FluentAssertions;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Tests.Capture;

/// <summary>
/// The out-of-process probe as the loop calls it: every way it can fail is an outcome on the result, never a throw.
/// </summary>
public sealed class NgxDriverProbeTests
{
    [Fact]
    public void AMissingProbeIsAnOutcomeThatNamesThePath()
    {
        NgxDriverState s = NgxDriverProbe.Run(Environment.ProcessId, @"C:\nowhere\fl-probe-nvapi.exe");

        s.Outcome.Should().Be(NgxProbeOutcome.ProbeMissing);
        s.Detail.Should().Contain("fl-probe-nvapi.exe");
        s.SrCreatedAndEvaluated.Should().BeFalse();
    }

    [Fact]
    public void RunAgainstThisTestProcessNeverThrowsAndNeverClaimsAFeature()
    {
        // The real binary is staged beside the host by the project file when the native build ran
        // first (CI does; a fresh clone may not). Whichever it is, this process renders nothing
        // through NGX, so the only honest outcomes are the ones that say so.
        NgxDriverState s = NgxDriverProbe.Run(Environment.ProcessId);

        s.Outcome.Should().BeOneOf(NgxProbeOutcome.Unanswered, NgxProbeOutcome.Degraded, NgxProbeOutcome.ProbeMissing,
            NgxProbeOutcome.ProbeFailed);
        s.SrCreatedAndEvaluated.Should().BeFalse();
        s.Readings.Should().Be(1);
        s.Describe().Should().StartWith("  NVIDIA driver NGX state");
    }
}
