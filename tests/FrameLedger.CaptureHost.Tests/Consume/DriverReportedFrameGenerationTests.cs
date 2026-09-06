using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The driver's FG word beside the <c>frame generation:</c> line (corrected 2026-09-06: it follows DLSS-G once the
/// feature exists), and the qualifier a counted-but-refused session prints.
/// </summary>
public sealed class DriverReportedFrameGenerationTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    private static readonly NgxDriverState _fgCreated = NgxDriverState.Parse(
        "NGXSTATE status=ANSWERED sr=0x605 rr=0x5 fg=0x605 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");

    private static readonly NgxDriverState _fgNotCreated = NgxDriverState.Parse(
        "NGXSTATE status=ANSWERED sr=0x605 rr=0x5 fg=0x5 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");

    /// <summary>k hooked presents per application frame; the token on the first; DLSS-G marked on it when tagged.</summary>
    private static List<FlFrameRecord> Stream(int appFrames, int k, bool tagged, byte unseenFrom = 0, int unseenAfter = int.MaxValue)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        ulong step = (ulong)(Stopwatch.Frequency / (60 * k));
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                bool first = p == 0;
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    OutputW = 2560,
                    OutputH = 1440,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler
                                            | FlMeasured.Fg | FlMeasured.FgCounts | FlMeasured.DxgiPresents),
                    Upscaler = (byte)(first ? FlUpscaler.Dlss : FlUpscaler.Unknown),
                    FgEvaluations = (byte)(first ? 1 : 0),
                    FgMode = (byte)(first && tagged ? FlFgMode.DlssG : FlFgMode.Unknown),
                    DxgiUnseen = f >= unseenAfter ? unseenFrom : (byte)0,
                });
                qpc += step;
            }
        }

        return stream;
    }

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, NgxDriverState ngx)
    {
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)_hooks,
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG),
            DxgiPresentSamples = (uint)stream.Count,
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(stream, Stopwatch.Frequency), ngx: ngx);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void ATaggedDlssGBesideTheDriversCreatedWordPrintsAgreement()
    {
        (MeasuredFacts facts, string text) = Render(Stream(100, 4, tagged: true), _fgCreated);

        facts.FgMode.Should().Be("DlssG");
        facts.FgModePrinted.Should().Be("DlssG");
        text.Should().Contain("frame generation: DlssG — the NVIDIA driver agrees: an NGX frame-generation feature");
        text.Should().Contain("(x4 FG)");
    }

    [Fact]
    public void AnActiveCountWithNoTagAndTheDriversCreatedWordIsDlssGDriverReported()
    {
        (MeasuredFacts facts, string text) = Render(Stream(100, 4, tagged: false), _fgCreated);

        facts.FgMode.Should().Be(MeasuredFacts.FgActiveUnidentified, "no tag named the technology");
        facts.FgModePrinted.Should().Be(MeasuredFacts.FgDlssGDriverReported, "the driver did, as identity only");
        text.Should().Contain("frame generation: DlssG (driver-reported:");
        text.Should().Contain("(x4 FG)", "the count is the record and is not changed by the driver");
        text.Should().NotContain("technology not identified");
    }

    [Fact]
    public void ATaggedDlssGBesideTheDriversNegativeIsADisagreementPrintedNotResolved()
    {
        (_, string text) = Render(Stream(100, 4, tagged: true), _fgNotCreated);

        text.Should().Contain("frame generation: DlssG — WARNING: the NVIDIA driver reports NO NGX frame-generation feature");
        text.Should().Contain("printed rather than resolved");
    }

    [Fact]
    public void ACountedNoneBesideTheDriversCreatedWordPrintsTheFactAndKeepsTheCount()
    {
        (MeasuredFacts facts, string text) = Render(Stream(200, 1, tagged: false), _fgCreated);

        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        facts.FgModePrinted.Should().Be(MeasuredFacts.FgNone, "the count decides none; the driver's word is identity only");
        text.Should().Contain("frame generation: None — the NVIDIA driver reports an NGX frame-generation feature created and evaluated");
        text.Should().Contain("the count above is the record and is not changed by it");
    }

    [Fact]
    public void WithoutAnAnswerTheFgLineIsByteIdenticalToBefore()
    {
        (_, string text) = Render(Stream(100, 4, tagged: false), NgxDriverState.NotRun);

        text.Should().Contain("frame generation: " + MeasuredFacts.FgActiveUnidentified);
        text.Should().NotContain("NVIDIA driver");
    }

    [Fact]
    public void ACountedSessionWhoseFactorWasRefusedSaysSoInsteadOfNoEvaluationObserved()
    {
        // DL:TB at FSR + DLSS-G, 2026-09-06: tokens counted, DXGI-counted presents, the pacer stopping
        // in the last eighth. The factor is refused (uniformity), and the qualifier must say that.
        List<FlFrameRecord> s = Stream(400, 1, tagged: true, unseenFrom: 3, unseenAfter: 200);

        (MeasuredFacts facts, string text) = Render(s, _fgCreated);

        facts.Fg!.Refusal.Should().Contain("changed during the session");
        text.Should().Contain("Presented FPS:");
        text.Should().Contain("WARNING: frame generation was counted (400 application-frame token(s), and DXGI counted 600 present(s) this hook never saw, so the Displayed rate is ABOVE this number) and its factor is REFUSED");
        text.Should().Contain("read it as neither Native nor Displayed");
        text.Should().NotContain("no evaluation was observed");
        text.Should().Contain("no FG factor: the frame-generation state changed during the session");
    }
}
