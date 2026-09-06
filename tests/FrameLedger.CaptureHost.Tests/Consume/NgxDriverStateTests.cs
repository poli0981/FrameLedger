using FluentAssertions;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The probe's machine line, parsed and merged: the words the owner measured on 2026-09-06 are the fixtures.
/// </summary>
public sealed class NgxDriverStateTests
{
    private const string _hellIsUs =
        "NGXSTATE status=ANSWERED sr=0x0000000000080605 rr=0x0000000000080005 fg=0x0000000000000005 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664";

    private const string _expedition33Override =
        "NGXSTATE status=ANSWERED sr=0x000000000008067F rr=0x0000000000080005 fg=0x000000000000001F ratio=0.0000 mode=2 preset=11 fgcount=0 fgpreset=0 fgmode=0 driver=61664";

    [Fact]
    public void TheAnsweredLineIsReadWholeAndTheHumanLinesAreIgnored()
    {
        string output = "fl-probe-nvapi --ngx-state 12468 — the driver's NGX feedback for one process\r\n\r\n"
                        + "  driver 616.64 (r616_41)\r\n  SR   feedback 0x0000000000080605 : INITIALIZED DLL_EXISTS CREATED EVALUATE ERR_NOT_FOUND\r\n"
                        + _hellIsUs + "\r\n";

        NgxDriverState s = NgxDriverState.Parse(output);

        s.Outcome.Should().Be(NgxProbeOutcome.Answered);
        s.Sr.Should().Be(0x80605UL);
        s.Rr.Should().Be(0x80005UL);
        s.Fg.Should().Be(0x5UL);
        s.Driver.Should().Be(61664u);
        s.SrCreatedAndEvaluated.Should().BeTrue("CREATED and EVALUATE are both set on Hell Is Us without an override");
        s.Readings.Should().Be(1);
        s.Answered.Should().Be(1);
        NgxOverrideFlags.Describe(s.Sr).Should().Be("INITIALIZED DLL_EXISTS CREATED EVALUATE ERR_NOT_FOUND");
        NgxOverrideFlags.Describe(s.Fg).Should().Be("INITIALIZED DLL_EXISTS");
        NgxOverrideFlags.Describe(0).Should().Be("-");
        NgxOverrideFlags.Describe(1UL << 40).Should().Be("0x10000000000", "an unknown bit is printed, never dropped");
    }

    [Fact]
    public void TheOverrideFieldsAreCarriedAndNamedAsOverrideFields()
    {
        NgxDriverState s = NgxDriverState.Parse(_expedition33Override);

        s.PerformanceMode.Should().Be(2u);
        s.RenderPreset.Should().Be(11u);
        s.ScalingRatio.Should().Be(0.0);
        s.SrCreatedAndEvaluated.Should().BeTrue();
        s.Describe().Should().Contain("performanceMode=2 renderPreset=11").And.Contain("the OVERRIDE's values")
            .And.Contain("the FG word does not reflect Streamline DLSS-G");
    }

    [Fact]
    public void UnansweredDegradedAndGarbageAreOutcomesNotExceptions()
    {
        NgxDriverState.Parse("NGXSTATE status=UNANSWERED nvapi=-160").Outcome.Should().Be(NgxProbeOutcome.Unanswered);
        NgxDriverState.Parse("NGXSTATE status=DEGRADED nvapi=-2").Outcome.Should().Be(NgxProbeOutcome.Degraded);
        NgxDriverState.Parse("nothing here").Outcome.Should().Be(NgxProbeOutcome.ProbeFailed);
        NgxDriverState.Parse("NGXSTATE status=ANSWERED sr=zz").Outcome.Should().Be(NgxProbeOutcome.ProbeFailed);
        NgxDriverState.Parse("NGXSTATE status=WHAT").Outcome.Should().Be(NgxProbeOutcome.ProbeFailed);
        NgxDriverState.Parse("NGXSTATE status=UNANSWERED nvapi=-160").SrCreatedAndEvaluated.Should().BeFalse();
        NgxDriverState.Parse("NGXSTATE status=UNANSWERED nvapi=-160").Describe().Should().Contain("did not answer").And.Contain("NvAPI -160");
    }

    [Fact]
    public void MergeKeepsTheLastAnswerCountsEveryReadingAndFlagsAChange()
    {
        NgxDriverState before = NgxDriverState.Parse(
            "NGXSTATE status=ANSWERED sr=0x5 rr=0x1 fg=0x5 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");
        NgxDriverState after = NgxDriverState.Parse(_hellIsUs);
        NgxDriverState silent = NgxDriverState.Parse("NGXSTATE status=UNANSWERED nvapi=-160");

        NgxDriverState merged = NgxDriverState.NotRun.Merge(before).Merge(after).Merge(silent);

        merged.Outcome.Should().Be(NgxProbeOutcome.Answered, "an earlier answer outranks a later silence");
        merged.Sr.Should().Be(0x80605UL, "the last ANSWERED reading is the state");
        merged.Readings.Should().Be(3);
        merged.Answered.Should().Be(2);
        merged.Changed.Should().BeTrue("the SR word gained CREATED | EVALUATE between readings");
        merged.Describe().Should().Contain("answered 2 of 3 probe(s), and the words CHANGED between readings");

        NgxDriverState same = NgxDriverState.NotRun.Merge(after).Merge(after);
        same.Changed.Should().BeFalse();
        same.Describe().Should().Contain("answered 2 of 2 probe(s); driver 616.64");
    }
}
