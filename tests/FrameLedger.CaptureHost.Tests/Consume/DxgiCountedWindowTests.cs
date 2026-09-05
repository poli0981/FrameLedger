using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// Displayed counted by DXGI (<c>FlFrameRecord.dxgiUnseen</c>, <c>20_OPEN_QUESTIONS</c> §H5 row P1-DXGI): on
/// Dying Light: The Beast (Streamline 2.8.0, DLSS FG ×4) the hook timed one present per application frame
/// while DXGI's own counter on the same chain moved by ≈ 3.9 — so the window counts
/// <c>Displayed = hooked + unseen</c>, says so on its own line, and every other title is byte-identical.
/// </summary>
public sealed class DxgiCountedWindowTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    private const FlRuntimeCensus _dyingLightAtDlss = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                      | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlDlssG
                                                      | FlRuntimeCensus.NvngxDlssG;

    private static readonly long _step = Stopwatch.Frequency / 70;

    /// <summary>
    /// The DL:TB shape: one hooked present per application frame (token drained, DLSS-G identity from the
    /// tags), each carrying <paramref name="unseen"/> DXGI-counted presents under the bit, or under no bit
    /// when <paramref name="claim"/> is false.
    /// </summary>
    private static List<FlFrameRecord> Stream(int appFrames, byte unseen, bool claim = true, int from = 0, int to = int.MaxValue)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        for (int f = 0; f < appFrames; f++)
        {
            bool inRange = f >= from && f < to;
            byte u = inRange ? unseen : (byte)0;
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
                DxgiUnseen = u,
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
            FgWindow.From(stream, Stopwatch.Frequency), Interposer("2.8.0.0"));
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void ThreeUnseenPerHookedPresentMakesAFourTimesTrioLabelledDxgiCounted()
    {
        List<FlFrameRecord> s = Stream(200, unseen: 3);

        (MeasuredFacts facts, string text) = Render(s, unseenTotal: 600, samples: 200);
        FgWindow w = facts.Fg!;

        w.Presents.Should().Be(200);
        w.DxgiUnseen.Should().Be(600);
        w.DxgiClaiming.Should().Be(200);
        w.DisplayedPresents.Should().Be(800);
        w.DxgiCounted.Should().BeTrue();
        w.Refusal.Should().BeNull();
        w.Factor.Should().BeApproximately(4.0, 0.001);
        w.IsActive.Should().BeTrue();
        w.PresentsPerBatch.Should().BeApproximately(4.0, 0.001);
        (w.DisplayedFps / w.NativeFps).Should().BeApproximately(4.0, 0.03, "Native × factor ≈ Displayed over ONE window");

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
    public void ZeroUnseenEverywhereIsByteIdenticalToTheWindowBeforeTheByteExisted()
    {
        List<FlFrameRecord> claimed = Stream(200, unseen: 0);
        List<FlFrameRecord> unclaimed = Stream(200, unseen: 0, claim: false);

        FgWindow a = FgWindow.From(claimed, Stopwatch.Frequency);
        FgWindow b = FgWindow.From(unclaimed, Stopwatch.Frequency);

        a.DxgiCounted.Should().BeFalse();
        a.DxgiClaiming.Should().Be(200);
        b.DxgiClaiming.Should().Be(0);
        a.DisplayedPresents.Should().Be(200);
        a.Factor.Should().Be(b.Factor);
        a.DisplayedFps.Should().Be(b.DisplayedFps);
        a.BucketFactors.Should().Equal(b.BucketFactors);

        // And the DL:TB shape with the count at 1.0 is still withheld, with the DXGI reading beside it.
        (MeasuredFacts facts, string text) = Render(claimed, unseenTotal: 0, samples: 200);
        facts.NoneWithheld.Should().NotBeNull();
        text.Should().Contain("`none` is WITHHELD").And.Contain("frame generation was off, or").And.NotContain("DXGI-COUNTED");
    }

    [Fact]
    public void APacerThatStopsHalfWayFailsUniformityLikeAnyOtherStateChange()
    {
        List<FlFrameRecord> s = Stream(400, unseen: 3, from: 0, to: 200);

        FgWindow w = FgWindow.From(s, Stopwatch.Frequency);

        w.DxgiUnseen.Should().Be(600);
        w.Refusal.Should().Contain("changed during the session");
        w.Factor.Should().BeNull();
        w.BatchRefusal.Should().Contain("changed during the session", "presents/batch takes the same numerator");
    }

    [Fact]
    public void ASaturatedByteIsASentinelAndTheFactorIsRefused()
    {
        List<FlFrameRecord> s = Stream(200, unseen: 3);
        FlFrameRecord r = s[100];
        r.DxgiUnseen = byte.MaxValue;
        s[100] = r;

        FgWindow w = FgWindow.From(s, Stopwatch.Frequency);

        w.DxgiSaturated.Should().Be(1);
        w.Refusal.Should().Contain("255");
        w.Factor.Should().BeNull();
    }

    [Fact]
    public void AByteUnderAClearBitIsNobodysCountAndAnHonestyViolation()
    {
        List<FlFrameRecord> s = Stream(200, unseen: 3, claim: false);

        (MeasuredFacts facts, _) = Render(s, unseenTotal: 0, samples: 0);

        facts.Fg!.DxgiUnseen.Should().Be(0, "an unclaimed byte is not read");
        facts.Fg.Factor.Should().BeApproximately(1.0, 0.001);
        facts.HonestyViolations.Should().Be(200);
    }
}
