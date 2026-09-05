using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// DXGI's own present counter against the hook's (<c>FlWriterState.dxgiPresentsUnseen / dxgiPresentSamples</c>,
/// <c>20_OPEN_QUESTIONS</c> §H5 A1.7): on the withheld shape the ratio names which side of DXGI the generated
/// presents fall on, and on every other shape it is a fact with no sentence of its own.
/// </summary>
public sealed class DxgiPresentCounterTests
{
    private const FlHookFamily _countingHooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.FgEvaluations;

    private const FlRuntimeCensus _dyingLightAtDlss = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                      | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlDlssG
                                                      | FlRuntimeCensus.NvngxDlssG;

    private static List<FlFrameRecord> Stream(int appFrames)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        ulong step = (ulong)(Stopwatch.Frequency / 60);
        for (int f = 0; f < appFrames; f++)
        {
            stream.Add(new FlFrameRecord
            {
                Qpc = qpc,
                SwapchainId = 1,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Fg | FlMeasured.FgCounts),
                FgEvaluations = 1,
                FgMode = (byte)FlFgMode.Unknown,
            });
            qpc += step;
        }

        return stream;
    }

    private static RuntimeModuleSet Interposer(string version) => new(
        [new RuntimeModuleInfo("sl.interposer.dll", @"D:\Games\title\sl.interposer.dll", version.Replace('.', ','),
            Version.Parse(version))],
        Snapshots: 1, Unreadable: 0);

    private static (MeasuredFacts Facts, string Text) Render(FlRuntimeCensus census, RuntimeModuleSet? modules,
        uint unseen, uint samples)
    {
        List<FlFrameRecord> stream = Stream(200);
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)_countingHooks,
            RuntimeCensus = (uint)census,
            DxgiPresentsUnseen = unseen,
            DxgiPresentSamples = samples,
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(stream, Stopwatch.Frequency), modules);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void ThreeUnseenPerHookedPresentOnTheWithheldShapeNamesADxgiLevelProducer()
    {
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 600, samples: 200);

        facts.NoneWithheld.Should().NotBeNull();
        facts.DxgiPresentsUnseen.Should().Be(600u);
        facts.DxgiPresentSamples.Should().Be(200u);
        facts.DxgiUnseenPerHookedPresent.Should().BeApproximately(3.0, 0.001);
        text.Should().Contain("`none` is WITHHELD");
        text.Should().Contain("DXGI's own counter on this chain saw 3.00 present(s) per hooked present");
        text.Should().Contain("the generated presents ARE DXGI presents");
        text.Should().Contain("§H5 row P1-DXGI");
        text.Should().NotContain("below DXGI");
    }

    [Fact]
    public void ZeroUnseenOnTheWithheldShapeSaysBelowDxgi()
    {
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 0, samples: 200);

        facts.DxgiUnseenPerHookedPresent.Should().Be(0.0);
        text.Should().Contain("`none` is WITHHELD");
        text.Should().Contain("DXGI's own counter on this chain saw nothing this hook did not");
        text.Should().Contain("§H5 row P3");
        text.Should().NotContain("ARE DXGI presents");
    }

    [Fact]
    public void ACounterNeverReadIsNullAndSaysNothing()
    {
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 0, samples: 0);

        facts.DxgiUnseenPerHookedPresent.Should().BeNull("no hooked present read the counter");
        text.Should().Contain("`none` is WITHHELD");
        text.Should().NotContain("DXGI's own counter");
    }

    [Fact]
    public void AValidatedNoneCarriesTheFactsButNoDxgiSentence()
    {
        // Cyberpunk 2077 on Streamline 2.7.1: `none` by count, never withheld, so the counter is a fact on
        // MeasuredFacts for the report's `runtime` block and not a sentence in a qualifier this shape does not print.
        (MeasuredFacts facts, string text) = Render(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG,
            Interposer("2.7.1.0"), unseen: 7, samples: 200);

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        facts.DxgiUnseenPerHookedPresent.Should().BeApproximately(0.035, 0.001);
        text.Should().Contain("frame generation: none");
        text.Should().NotContain("DXGI's own counter");
    }
}
