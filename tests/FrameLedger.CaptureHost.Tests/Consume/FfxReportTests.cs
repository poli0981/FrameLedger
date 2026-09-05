using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The AMD route on the reading side: identity from the leaf, <c>FSR_FG</c> from the generated
/// batch, <c>none</c> from the count, and the second count printed beside the first.
/// </summary>
/// <remarks>
/// The synthetic records are the shape <c>dllmain.cpp</c>'s ffx-api arms write and
/// <c>guard_test.cpp</c>'s <c>[ffx]</c> cases assert against the real Overlay: identity, params
/// and the count land on the present that DRAINED the dispatch, and the other K−1 presents of
/// an application frame carry the bits with the honest zeros.
/// </remarks>
public sealed class FfxReportTests
{
    private static List<FlFrameRecord> FfxStream(int frames, int k, FlUpscaler identity, bool fgDispatch,
        int evaluationsPerFrame = 1, int hz = 240)
    {
        var list = new List<FlFrameRecord>(frames * k);
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / hz;
        for (int f = 0; f < frames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                bool drained = p == 0;
                list.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    OutputW = 2560,
                    OutputH = 1440,
                    Api = (byte)FlApi.D3D12,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler
                                            | FlMeasured.Fg | FlMeasured.FgCounts
                                            | (drained ? FlMeasured.UpscalerParams : FlMeasured.None)),
                    Upscaler = (byte)(drained ? identity : FlUpscaler.Unknown),
                    FgMode = (byte)(drained && fgDispatch ? FlFgMode.FsrFg : FlFgMode.Unknown),
                    FgEvaluations = (byte)(drained ? evaluationsPerFrame : 0),
                    RenderW = (ushort)(drained ? 1485 : 0),
                    RenderH = (ushort)(drained ? 835 : 0),
                    UpscalerQuality = (byte)(drained ? 0xFF : 0),
                    UpscalerSharpness = (byte)(drained ? 0xFF : 0),
                });
                qpc += (ulong)step;
            }
        }

        return list;
    }

    private static FlWriterState Writer(FlRuntimeCensus census) => new()
    {
        HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity
                                    | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations),
        RuntimeCensus = (uint)(FlRuntimeCensus.Ran | census),
    };

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, FlWriterState writer)
    {
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(stream, Stopwatch.Frequency));
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void LiesOfPAtFsr3PlusFrameGenerationPrintsTheTrioAndNoWarning()
    {
        // HANDOFF 7c's acceptance row A1, on the reading side: the monolith is in the census's FG
        // group, which used to be exactly what printed the WARNING — measured now, the trio
        // prints and the qualifier does not.
        List<FlFrameRecord> stream = FfxStream(frames: 80, k: 2, FlUpscaler.Fsr3, fgDispatch: true);

        (MeasuredFacts facts, string text) = Render(stream, Writer(FlRuntimeCensus.AmdFfxDx12));

        facts.Upscaler.Should().Be("Fsr3");
        facts.FgMode.Should().Be("FsrFg");
        facts.FgFactor.Should().BeApproximately(2.0, 0.01);
        facts.HonestyViolations.Should().Be(0);
        text.Should().Contain("Native FPS").And.Contain("Displayed FPS").And.Contain("x2 FG");
        text.Should().Contain("upscaler: Fsr3");
        text.Should().Contain("frame generation: FsrFg");
        text.Should().NotContain("WARNING");
        text.Should().NotContain("MAY include generated frames");
    }

    [Fact]
    public void FrameGenerationOffOnTheMonolithIsNoneByTheUpscaleCount()
    {
        // Row A2: no PREPARE ever, so the writer counted UPSCALE dispatches — one per present —
        // and the consumer reads the measured `none` from the ratio, not from the census.
        List<FlFrameRecord> stream = FfxStream(frames: 200, k: 1, FlUpscaler.Fsr3, fgDispatch: false);

        (MeasuredFacts facts, string text) = Render(stream, Writer(FlRuntimeCensus.AmdFfxDx12));

        facts.Upscaler.Should().Be("Fsr3");
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        facts.FgFactor.Should().BeApproximately(1.0, 0.01);
        text.Should().Contain("  FPS: ").And.Contain("frame generation: none");
        text.Should().Contain("ffx-api PREPARE / UPSCALE dispatch");
        text.Should().NotContain("Native FPS").And.NotContain("Presented FPS");
    }

    [Fact]
    public void TheSdk2xUpscalerLeafIsNamedFsrWithoutAVersion()
    {
        // The enumerator is a token; the report carries the sentence, and never guesses FSR4.
        List<FlFrameRecord> stream = FfxStream(frames: 80, k: 2, FlUpscaler.FsrUnversioned, fgDispatch: true);

        (MeasuredFacts facts, string text) = Render(stream,
            Writer(FlRuntimeCensus.AmdFfxUpscaler | FlRuntimeCensus.AmdFfxFrameGeneration));

        facts.Upscaler.Should().Be(MeasuredFacts.UpscalerFsrUnversioned);
        text.Should().Contain("upscaler: FSR (3.1 or 4");
        text.Should().NotContain("Fsr4").And.NotContain("FsrUnversioned");
        text.Should().Contain("frame generation: FsrFg");
    }

    [Fact]
    public void TheCensusPrintsTwoCountsAndTheirAgreement()
    {
        List<FlFrameRecord> stream = FfxStream(frames: 60, k: 2, FlUpscaler.Fsr3, fgDispatch: true);

        FfxCensus c = FfxCensus.From(stream);

        c.Records.Should().Be(120);
        c.UpscaleDrained.Should().Be(60);
        c.FgDispatchDrained.Should().Be(60);
        c.Frames.Should().Be(60);
        c.PresentsPerFrame.Should().BeApproximately(2.0, 0.001);
        c.FramesPerUpscale.Should().BeApproximately(1.0, 0.001, "the PREPARE and UPSCALE counts agree");
        c.FgDispatchesPerFrame.Should().BeApproximately(1.0, 0.001);
        c.Describe().Should().Contain("frames/upscale-drained=1.00").And.Contain("upscale-drained=60");
    }

    [Fact]
    public void ADoubledFrameCountReadsTwoOnTheAgreementRatio()
    {
        // THE SHAPE THE SECOND COUNT EXISTS TO CATCH: a writer that hooked the loader as well as
        // the leaf counts every PREPARE twice. presents/frame then reads 1.0 at ×2 — the factor
        // rule 6 forbids reaching by arithmetic — and only this ratio says why.
        List<FlFrameRecord> stream = FfxStream(frames: 60, k: 2, FlUpscaler.Fsr3, fgDispatch: true,
            evaluationsPerFrame: 2);

        FfxCensus c = FfxCensus.From(stream);

        c.PresentsPerFrame.Should().BeApproximately(1.0, 0.001);
        c.FramesPerUpscale.Should().BeApproximately(2.0, 0.001);
        c.Describe().Should().Contain("frames/upscale-drained=2.00");
    }

    [Fact]
    public void AWindowWithNoFfxDispatchSaysSoRatherThanPrintingZeros()
    {
        // A Streamline title: the identity hook ran, nothing FSR-shaped arrived.
        List<FlFrameRecord> stream = FfxStream(frames: 20, k: 1, FlUpscaler.Dlss, fgDispatch: false);

        FfxCensus c = FfxCensus.From(stream);

        c.SawAnything.Should().BeFalse();
        c.Describe().Should().Contain("no UPSCALE or FRAMEGENERATION dispatch reached an ffx-api leaf or the FSR 3.0 host facade");
        FfxCensus.From([]).Describe().Should().Contain("no record claimed FL_MEASURED_UPSCALER");
    }

    [Fact]
    public void CyberpunkAtFsr3PrintsFsr3IdentityFromTheHostRouteBesideTheMonolithsFrameGeneration()
    {
        // Cyberpunk 2077's measured shape (2026-09-04, §H11): the FSR 3.0 HOST DLL upscales through its
        // named export while the 1.1.x monolith generates through ffxDispatch. The writer's records are
        // the same shape as any FSR route's -- identity FSR3 on the present that drained the host's
        // UPSCALE, FsrFg on the one that drained the monolith's FRAMEGENERATION -- and the report
        // prints the trio at ×2 with no WARNING. Before the host row this title printed `upscaler: N/A`.
        List<FlFrameRecord> stream = FfxStream(frames: 50, k: 2, FlUpscaler.Fsr3, fgDispatch: true);

        (MeasuredFacts facts, string text) = Render(stream, Writer(FlRuntimeCensus.FfxFsr3 | FlRuntimeCensus.FfxFsr3Upscaler
                                                                   | FlRuntimeCensus.FfxFrameInterpolation | FlRuntimeCensus.AmdFfxDx12));

        facts.Upscaler.Should().Be("Fsr3");
        facts.FgMode.Should().Be("FsrFg");
        facts.FgFactor.Should().BeApproximately(2.0, 0.05);
        text.Should().Contain("upscaler: Fsr3");
        text.Should().Contain("frame generation: FsrFg");
        text.Should().Contain("x2 FG");
        text.Should().NotContain("WARNING");
        text.Should().NotContain("upscaler: N/A");
        FfxCensus.From(stream).Describe().Should().Contain("presents/frame=2.00");
    }
}
