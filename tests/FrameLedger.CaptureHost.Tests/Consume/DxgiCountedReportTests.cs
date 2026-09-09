using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Metrics;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The report's side of a DXGI-counted Displayed (<c>20_OPEN_QUESTIONS</c> §H5 row P1-DXGI): the trio labelled
/// DXGI-COUNTED, the counted <c>none</c> with DXGI's agreement beside it, and an unclaimed byte as an honesty
/// violation. The window's own arithmetic is tested with <c>Domain.Metrics.FgWindow</c>.
/// </summary>
public sealed class DxgiCountedReportTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    private const FlRuntimeCensus _dyingLightAtDlss = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                      | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlDlssG
                                                      | FlRuntimeCensus.NvngxDlssG;

    private static readonly long _step = Stopwatch.Frequency / 70;

    private static List<FlFrameRecord> Stream(int appFrames, byte unseen, bool claim = true)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        for (int f = 0; f < appFrames; f++)
        {
            stream.Add(new FlFrameRecord
            {
                Qpc = qpc,
                SwapchainId = 1,
                OutputW = 2560,
                OutputH = 1440,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler
                                        | FlMeasured.Fg | FlMeasured.FgCounts
                                        | (claim ? FlMeasured.DxgiPresents : FlMeasured.None)),
                Upscaler = (byte)FlUpscaler.Dlss,
                FgEvaluations = 1,
                FgMode = (byte)FlFgMode.DlssG,
                FeatureFlags = (byte)FlFeatureFlags.RayReconstructionObserved,
                DxgiUnseen = unseen,
            });
            qpc += (ulong)_step;
        }

        return stream;
    }

    private static RuntimeModuleSet Interposer(string version) => new(
        [new RuntimeModuleInfo("sl.interposer.dll", @"D:\Games\title\sl.interposer.dll", version.Replace('.', ','),
            Version.Parse(version))],
        Snapshots: 1, Unreadable: 0);

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, uint unseenTotal, uint samples)
    {
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)_hooks,
            RuntimeCensus = (uint)_dyingLightAtDlss,
            DxgiPresentsUnseen = unseenTotal,
            DxgiPresentSamples = samples,
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency), Interposer("2.8.0.0"));
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void ThreeUnseenPerHookedPresentPrintsAFourTimesTrioLabelledDxgiCounted()
    {
        (MeasuredFacts facts, string text) = Render(Stream(200, unseen: 3), unseenTotal: 600, samples: 200);

        facts.Fg!.DisplayedPresents.Should().Be(800);
        facts.NoneWithheld.Should().BeNull("the count is no longer 1.0, so there is nothing to withhold");
        facts.FgMode.Should().Be("DlssG");
        text.Should().Contain("Native FPS:").And.Contain("-> Displayed FPS:").And.Contain("(x4 FG)");
        text.Should().Contain("over 800 present(s)");
        text.Should().Contain("Displayed is DXGI-COUNTED: 600 of those present(s)");
        text.Should().Contain("a count DXGI made, not this hook");
        text.Should().Contain("dxgi-unseen=600");
        text.Should().NotContain("WITHHELD").And.NotContain("Presented FPS:");
    }

    [Fact]
    public void ACountOfOneBesideAReadCounterWithNothingUnseenIsNoneWithDxgisAgreement()
    {
        (MeasuredFacts facts, string text) = Render(Stream(200, unseen: 0), unseenTotal: 0, samples: 200);

        facts.NoneWithheld.Should().BeNull();
        text.Should().Contain("frame generation: none").And.Contain("DXGI's own present counter agrees")
            .And.NotContain("WITHHELD").And.NotContain("DXGI-COUNTED");
    }

    [Fact]
    public void AByteUnderAClearBitIsNobodysCountAndAnHonestyViolation()
    {
        (MeasuredFacts facts, _) = Render(Stream(200, unseen: 3, claim: false), unseenTotal: 0, samples: 0);

        facts.Fg!.DxgiUnseen.Should().Be(0, "an unclaimed byte is not read");
        facts.Fg.Factor.Should().BeApproximately(1.0, 0.001);
        facts.HonestyViolations.Should().Be(200);
    }
}
