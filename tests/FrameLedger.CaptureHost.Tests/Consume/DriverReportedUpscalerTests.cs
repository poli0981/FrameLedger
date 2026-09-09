using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// <c>03_METRICS</c> §Upscaling, the driver-reported rung: the driver's word names DLSS only where no hook did,
/// stands beside a hook's name as agreement or a printed disagreement, and never claims more than an identity.
/// </summary>
public sealed class DriverReportedUpscalerTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    private static readonly NgxDriverState _created = NgxDriverState.Parse(
        "NGXSTATE status=ANSWERED sr=0x605 rr=0x90001 fg=0x90001 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");

    private static readonly NgxDriverState _notCreated = NgxDriverState.Parse(
        "NGXSTATE status=ANSWERED sr=0x5 rr=0x1 fg=0x1 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");

    /// <summary>Records with, or without, a hooked upscaler identity; the hook family is installed either way.</summary>
    private static List<FlFrameRecord> Stream(FlUpscaler identity)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        ulong step = (ulong)(Stopwatch.Frequency / 60);
        for (int f = 0; f < 120; f++)
        {
            stream.Add(new FlFrameRecord
            {
                Qpc = qpc,
                SwapchainId = 1,
                OutputW = 2560,
                OutputH = 1440,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler),
                Upscaler = (byte)identity,
            });
            qpc += step;
        }

        return stream;
    }

    private static (MeasuredFacts Facts, string Text) Render(FlUpscaler identity, NgxDriverState? ngx)
    {
        List<FlFrameRecord> stream = Stream(identity);
        var writer = new FlWriterState { HooksInstalledMask = (uint)_hooks, RuntimeCensus = (uint)FlRuntimeCensus.Ran };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0, ngx: ngx);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void LiesOfPShapeNoHookNamedAnUpscalerAndTheDriverSaysCreatedIsDlssDriverReported()
    {
        (MeasuredFacts facts, string text) = Render(FlUpscaler.Unknown, _created);

        facts.Upscaler.Should().BeNull("the hook ran and decoded nothing on an NGX-direct title");
        facts.UpscalerDriverReported.Should().Be(MeasuredFacts.UpscalerDlssDriverReported);
        facts.UpscalerDriverNote.Should().BeNull("the identity line already carries the attribution");
        text.Should().Contain("upscaler: Dlss (driver-reported:");
        text.Should().Contain("not counted by this hook");
        text.Should().NotContain("upscaler: N/A");
    }

    [Fact]
    public void AHookedDlssBesideTheDriversCreatedWordPrintsAgreement()
    {
        (MeasuredFacts facts, string text) = Render(FlUpscaler.Dlss, _created);

        facts.Upscaler.Should().Be("Dlss");
        facts.UpscalerDriverReported.Should().BeNull("a hook's name always outranks the driver's word");
        text.Should().Contain("upscaler: Dlss — the NVIDIA driver agrees");
        text.Should().NotContain("driver-reported:");
    }

    [Fact]
    public void AHookedDlssBesideTheDriversNegativeIsADisagreementPrintedNotResolved()
    {
        (MeasuredFacts facts, string text) = Render(FlUpscaler.Dlss, _notCreated);

        facts.Upscaler.Should().Be("Dlss");
        text.Should().Contain("upscaler: Dlss — WARNING: the NVIDIA driver reports NO NGX super-resolution feature created");
        text.Should().Contain("printed rather than resolved");
    }

    [Fact]
    public void NoHookNameAndTheDriversNegativeKeepsNaWithTheNegativeAttributed()
    {
        (MeasuredFacts facts, string text) = Render(FlUpscaler.Unknown, _notCreated);

        facts.UpscalerDriverReported.Should().BeNull();
        text.Should().Contain("upscaler: N/A (an upscaler hook ran");
        text.Should().Contain("— the NVIDIA driver reports no NGX super-resolution feature created in this process (driver-reported; it says nothing about FSR or XeSS)");
    }

    [Fact]
    public void AnFsrIdentityBesideTheDriversCreatedWordIsPrintedAsAlso()
    {
        (_, string text) = Render(FlUpscaler.Fsr3, _created);

        text.Should().Contain("upscaler: Fsr3 — the NVIDIA driver also reports an NGX super-resolution feature");
    }

    [Fact]
    public void WithoutAProbeOrWithoutAnAnswerTheLineIsByteIdenticalToBefore()
    {
        (_, string none) = Render(FlUpscaler.Unknown, null);
        (_, string unanswered) = Render(FlUpscaler.Unknown, NgxDriverState.Parse("NGXSTATE status=UNANSWERED nvapi=-160"));
        (_, string missing) = Render(FlUpscaler.Unknown, NgxDriverState.Of(NgxProbeOutcome.ProbeMissing, @"C:\nowhere\fl-probe-nvapi.exe"));

        foreach (string text in new[] { none, unanswered, missing })
        {
            text.Should().Contain("upscaler: N/A (an upscaler hook ran");
            text.Should().NotContain("NVIDIA driver");
        }
    }
}
