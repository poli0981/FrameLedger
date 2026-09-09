using System.Diagnostics;
using System.Text.RegularExpressions;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Metrics;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// <c>20_OPEN_QUESTIONS</c> §H5 case 3: a counted 1.0 on Streamline 2.8 beside a loaded DLSS-G
/// plugin is withheld from <c>none</c>, and every validated <c>none</c> stays byte-identical.
/// </summary>
/// <remarks>
/// The census words here are the ones real titles published: Dying Light: The Beast at DLSS
/// (<c>sl.dlss_g.dll</c> set), Cyberpunk 2077 on 2.7.1 (<c>nvngx_dlssg.dll</c> only), the UE5
/// shape with the NGX plugin and no Streamline plugin. The gate is keyed so that only the
/// first of those changes, and these cases are what pin that.
/// </remarks>
public sealed partial class H5CaseThreeTests
{
    [GeneratedRegex(@"[x×] ?\d", RegexOptions.None, matchTimeoutMilliseconds: 1000)]
    private static partial Regex FgFactorShape();

    private const FlHookFamily _countingHooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.FgEvaluations;

    private const FlRuntimeCensus _dyingLightAtDlss = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                      | FlRuntimeCensus.SlDlss | FlRuntimeCensus.SlDlssG
                                                      | FlRuntimeCensus.NvngxDlssG;

    private const FlRuntimeCensus _cyberpunkOn271 = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                    | FlRuntimeCensus.NvngxDlssG;

    private const FlRuntimeCensus _ue5NgxOnly = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer
                                                | FlRuntimeCensus.NvngxDlssG | FlRuntimeCensus.AmdFfxFrameGeneration
                                                | FlRuntimeCensus.AmdFfxUpscaler;

    /// <summary>k presents per application frame; the token is counted on the first of each.</summary>
    private static List<FlFrameRecord> Stream(int appFrames, int k)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / (60 * k);
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Fg | FlMeasured.FgCounts),
                    FgEvaluations = (byte)(p == 0 ? 1 : 0),
                    FgMode = (byte)FlFgMode.Unknown,
                });
                qpc += (ulong)step;
            }
        }

        return stream;
    }

    private static RuntimeModuleSet Interposer(string version) => new(
        [new RuntimeModuleInfo("sl.interposer.dll", @"D:\Games\title\sl.interposer.dll", version.Replace('.', ','),
            Version.Parse(version))],
        Snapshots: 1, Unreadable: 0);

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, FlRuntimeCensus census,
        RuntimeModuleSet? modules, FlHookFamily hooks = _countingHooks, uint dxgiSamples = 0)
    {
        // dxgiSamples 0 is the shape every case below was written against: DXGI's counter not read,
        // which since Leg 0 (2026-09-06) is the only shape the gate still withholds on.
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)hooks,
            RuntimeCensus = (uint)census,
            DxgiPresentSamples = dxgiSamples,
        };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(FrameSampleMapper.Map(stream), Stopwatch.Frequency), modules);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void DyingLightTheBeastShapeWithholdsNoneAndPrintsThePresentedLineWithTheH5Warning()
    {
        (MeasuredFacts facts, string text) = Render(Stream(200, 1), _dyingLightAtDlss, Interposer("2.8.0.0"));

        facts.NoneWithheld.Should().NotBeNull();
        facts.FgMode.Should().StartWith(MeasuredFacts.FgNoneWithheldPrefix);
        facts.FgMode.Should().Contain("sl.dlss_g.dll").And.Contain("2.8.0.0");
        facts.FgFactor.Should().BeApproximately(1.0, 0.01, "the count is still the record");
        facts.InterposerVersion.Should().Be(new Version(2, 8, 0, 0));

        text.Should().Contain("Presented FPS:");
        text.Should().Contain("`none` is WITHHELD");
        text.Should().Contain("§H5 case 3");
        text.Should().Contain("counts APPLICATION frames");
        text.Should().Contain("the Displayed rate, are unknown");
        text.Should().Contain("frame generation: NOT stated");
        text.Should().Contain("frame generation: N/A (`none` withheld");
        text.Should().NotContain("  FPS: ");
        text.Should().NotContain("frame generation: none");
        text.Should().NotContain("read it as Displayed, not Native");
        text.Should().NotContain("Native FPS");
        FgFactorShape().IsMatch(text).Should().BeFalse("a withheld none prints no factor");
    }

    [Fact]
    public void LegZeroDlssgOffOnStreamline28IsNoneWhenDxgiCountedNothingUnseen()
    {
        // 2026-09-06, 07:45 and 07:55: Dying Light: The Beast with frame generation OFF, census still
        // 0x25F0F (the plugin is a startup-time load), presents = tokens, `unseen=0 over samples=3659`.
        // The 2.8.0 pacer's presents are DXGI presents (row P1-DXGI), so DXGI counting none is `none`.
        (MeasuredFacts facts, string text) = Render(Stream(200, 1), _dyingLightAtDlss, Interposer("2.8.0.0"),
            dxgiSamples: 3659);

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        facts.NoneBesideDxgi.Should().Contain("agrees").And.Contain("3659");
        text.Should().Contain("  FPS: ");
        text.Should().Contain("frame generation: none");
        text.Should().Contain("DXGI's own present counter agrees: 0 unseen over 3659 hooked present(s)");
        text.Should().NotContain("WITHHELD").And.NotContain("Presented FPS:");
    }

    [Fact]
    public void TheWithheldReasonNamesTheCounterThatWasNotRead()
    {
        (MeasuredFacts facts, _) = Render(Stream(200, 1), _dyingLightAtDlss, Interposer("2.8.0.0"));

        facts.NoneWithheld.Should().Contain("was not read this session").And.Contain("P1-DXGI");
    }

    [Fact]
    public void CyberpunkOnStreamline271WithNvngxDlssgLoadedKeepsNone()
    {
        // The measured census of the title whose off / ×3 / ×4 legs validated the count (§S31 row P1):
        // nvngx_dlssg.dll loaded, sl.dlss_g.dll NOT — and the interposer is 2.7.1. Both conjuncts
        // of the gate are clear, so this is the exact output the run landed on.
        (MeasuredFacts facts, string text) = Render(Stream(200, 1), _cyberpunkOn271, Interposer("2.7.1.0"));

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("  FPS: ").And.Contain("frame generation: none");
        text.Should().NotContain("WITHHELD").And.NotContain("Presented FPS");
    }

    [Fact]
    public void AStreamline27TitleThatLoadsThePluginKeepsNoneBelowTheVersionGate()
    {
        // The plugin alone is not the shape: it is what Cyberpunk would be if it loaded sl.dlss_g.dll,
        // and Cyberpunk's generated presents demonstrably reach this hook on 2.7.1.
        (MeasuredFacts facts, string text) = Render(Stream(200, 1), _dyingLightAtDlss, Interposer("2.7.1.0"));

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("frame generation: none");
    }

    [Fact]
    public void TheUe5ShapeWithNvngxDlssgButNoStreamlinePluginKeepsNoneWhateverTheInterposerVersion()
    {
        // Expedition 33 and Hell Is Us with frame generation off: nvngx_dlssg.dll is loaded through NGX
        // on every UE5 title, and their ×3 / ×4 legs read 2.97 / 4.00 through this very hook. A gate
        // keyed on that module would have turned three validated results into warnings.
        (MeasuredFacts facts, string text) = Render(Stream(200, 1), _ue5NgxOnly, Interposer("2.9.0.0"));

        facts.NoneWithheld.Should().BeNull();
        facts.FgMode.Should().Be(MeasuredFacts.FgNone);
        text.Should().Contain("frame generation: none");
    }

    [Fact]
    public void AnUnreadableInterposerVersionWithThePluginLoadedWithholdsNoneAndSaysWhy()
    {
        // No snapshot at all, and a snapshot that failed, are the same absence: the discriminator is
        // missing, and an N/A is the honest answer over a none this consumer cannot discriminate.
        (MeasuredFacts none, string noneText) = Render(Stream(200, 1), _dyingLightAtDlss, modules: null);
        (MeasuredFacts failed, string failedText) = Render(Stream(200, 1), _dyingLightAtDlss,
            new RuntimeModuleSet([], Snapshots: 2, Unreadable: 2));

        none.NoneWithheld.Should().Contain("could not be read");
        failed.NoneWithheld.Should().Contain("could not be read");
        noneText.Should().Contain("Presented FPS:").And.Contain("WITHHELD");
        failedText.Should().Contain("Presented FPS:").And.Contain("WITHHELD");
    }

    [Fact]
    public void StreamlineOneFiveSixIsUntouched()
    {
        // The Witcher 3's interposer: no hook installs, no count exists, so the gate has nothing to
        // withhold and the census-refined N/A is what prints — exactly as before this gate existed.
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)FlHookFamily.Present,
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer),
        };
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        for (uint i = 0; i < 60; i++)
        {
            stream.Add(new FlFrameRecord
            {
                FrameIndex = i,
                Qpc = qpc,
                SwapchainId = 1,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs),
            });
            qpc += (ulong)(Stopwatch.Frequency / 120);
        }

        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0, null, Interposer("1.5.6.0"));
        string text = SessionReport.Render(facts);

        facts.NoneWithheld.Should().BeNull();
        facts.InterposerVersion.Should().Be(new Version(1, 5, 6, 0));
        text.Should().Contain("no upscaler hook ran, though an upscaler runtime is loaded: sl.interposer.dll");
        text.Should().Contain("cannot include in-process generated frames");
        text.Should().NotContain("WITHHELD");
    }

    [Fact]
    public void TheGateNeverFiresOnAnActiveFactor()
    {
        // ×4 counted on the very shape the gate keys on: the trio prints, because a factor that clears
        // the cadence threshold is a measurement of generated presents reaching this hook.
        (MeasuredFacts facts, string text) = Render(Stream(100, 4), _dyingLightAtDlss, Interposer("2.8.0.0"));

        facts.NoneWithheld.Should().BeNull();
        facts.Fg!.IsActive.Should().BeTrue();
        text.Should().Contain("Native FPS").And.Contain("Displayed FPS").And.Contain("x4 FG");
        text.Should().NotContain("WITHHELD").And.NotContain("WARNING");
    }

    [Fact]
    public void TheCensusNamesEveryModuleTheSnapshotAsksAbout()
    {
        CensusNames.ModuleFileNames.Should().Contain(["sl.interposer.dll", "sl.dlss_g.dll", "nvngx_dlssg.dll", "ffx_fsr3_x64.dll"]);
        CensusNames.ModuleFileNames.Should().Contain("_nvngx.dll", "the a / b pairs are split into their members");
        CensusNames.ModuleFileNames.Should().OnlyContain(n => !n.Contains(" / ", StringComparison.Ordinal));
    }

    [Fact]
    public void MergeKeepsTheFirstVersionSeenAndCountsEverySnapshot()
    {
        RuntimeModuleSet first = Interposer("2.8.0.0");
        RuntimeModuleSet later = new(
            [
                new RuntimeModuleInfo("sl.interposer.dll", @"D:\other\sl.interposer.dll", "2,7,1,0", new Version(2, 7, 1, 0)),
                new RuntimeModuleInfo("sl.dlss_g.dll", @"D:\Games\title\sl.dlss_g.dll", "310,6,0,0", new Version(310, 6, 0, 0)),
            ],
            Snapshots: 1, Unreadable: 1);

        RuntimeModuleSet merged = first.Merge(later);

        merged.VersionOf("SL.INTERPOSER.DLL").Should().Be(new Version(2, 8, 0, 0), "first seen wins, case-insensitively");
        merged.VersionOf("sl.dlss_g.dll").Should().Be(new Version(310, 6, 0, 0));
        merged.Snapshots.Should().Be(2);
        merged.Unreadable.Should().Be(1);
        RuntimeModuleSet.Empty.VersionOf("sl.interposer.dll").Should().BeNull();
    }
}
