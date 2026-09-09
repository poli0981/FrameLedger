using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Metrics;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// DXGI's own present counter in the writer state (<c>FlWriterState.dxgiPresentsUnseen / dxgiPresentSamples</c>,
/// <c>20_OPEN_QUESTIONS</c> §H5 A1.7) beside a counted <c>none</c>: since Leg 0 (2026-09-06) a READ counter
/// decides the Streamline 2.8.0 shape, and on every shape it is a second witness printed beside the count.
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
            FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency), modules);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void ZeroUnseenOverAReadCounterOnTheStreamline28ShapeIsNoneWithDxgiAgreeing()
    {
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 0, samples: 200);

        facts.DxgiUnseenPerHookedPresent.Should().Be(0.0);
        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("frame generation: none");
        text.Should().Contain("DXGI's own present counter agrees: 0 unseen over 200 hooked present(s)");
        text.Should().NotContain("WITHHELD");
    }

    [Fact]
    public void ACounterNeverReadStillWithholdsAndSaysSo()
    {
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 0, samples: 0);

        facts.DxgiUnseenPerHookedPresent.Should().BeNull("no hooked present read the counter");
        facts.NoneWithheld.Should().Contain("was not read this session");
        facts.NoneBesideDxgi.Should().BeNull();
        text.Should().Contain("`none` is WITHHELD");
        text.Should().NotContain("DXGI's own present counter");
    }

    [Fact]
    public void UnseenPresentsInTheWriterStateButNotInTheRecordsIsAContradictionNotANone()
    {
        // The two words come from the same writer, so a total the records do not carry is a defect;
        // it is refused rather than read as either `none` or a factor.
        (MeasuredFacts facts, string text) = Render(_dyingLightAtDlss, Interposer("2.8.0.0"), unseen: 600, samples: 200);

        facts.DxgiUnseenPerHookedPresent.Should().BeApproximately(3.0, 0.001);
        facts.NoneWithheld.Should().Contain("disagree").And.Contain("600");
        text.Should().Contain("`none` is WITHHELD");
        text.Should().NotContain("frame generation: none");
    }

    [Fact]
    public void AValidatedNoneOnStreamline27PrintsTheCounterAsASecondWitness()
    {
        (MeasuredFacts facts, string text) = Render(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.NvngxDlssG,
            Interposer("2.7.1.0"), unseen: 7, samples: 200);

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("frame generation: none");
        text.Should().Contain("DXGI's own present counter read 7 unseen over 200 hooked present(s), inside the `none` ceiling");
    }
}
