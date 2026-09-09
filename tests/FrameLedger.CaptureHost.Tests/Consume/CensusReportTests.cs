using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The line under Presented FPS, and what the runtime census may and may not turn it into.
/// </summary>
/// <remarks>
/// The two cases that motivated the census printed the same report until 2026-09-03: a 2D
/// title with no upscaler at all, and a Streamline title with upscaling switched off in its
/// settings. These pin that they now differ — and that neither becomes <c>none</c>, because
/// a census is a module list, not a hook, and a statically linked FSR has no module to list.
/// </remarks>
public sealed class CensusReportTests
{
    private static FlFrameRecord Present(uint index, ulong qpc, FlMeasured extra = FlMeasured.None,
        byte upscaler = 0, byte fgMode = 0) => new()
        {
            FrameIndex = index,
            Qpc = qpc,
            SwapchainId = 1,
            OutputW = 1920,
            OutputH = 1080,
            Api = (byte)FlApi.D3D11,
            MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | extra),
            Upscaler = upscaler,
            FgMode = fgMode,
        };

    private static List<FlFrameRecord> Stream(int n, FlMeasured extra = FlMeasured.None, byte upscaler = 0, byte fgMode = 0)
    {
        var list = new List<FlFrameRecord>(n);
        ulong qpc = 1_000_000;
        long step = Stopwatch.Frequency / 120;
        for (int i = 0; i < n; i++)
        {
            list.Add(Present((uint)i, qpc, extra, upscaler, fgMode));
            qpc += (ulong)step;
        }

        return list;
    }

    private static string Render(List<FlFrameRecord> stream, FlWriterState writer) =>
        SessionReport.Render(MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0));

    [Fact]
    public void ATwoDTitleWithNoRuntimeCannotBeHidingGeneratedFrames()
    {
        // (a) present-only writer, census ran, no vendor runtime in the process. The honest
        // sentence is stronger than N/A and weaker than `none`: the number cannot include
        // in-process generated frames, and the two holes are named in the same breath.
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)FlHookFamily.Present,
            RuntimeCensus = (uint)FlRuntimeCensus.Ran,
        };

        string text = Render(Stream(120), writer);

        text.Should().Contain("Presented FPS:");
        text.Should().Contain("cannot include in-process generated frames");
        text.Should().Contain("statically linked FSR3-FG");
        text.Should().NotContain("WARNING");
        text.Should().NotContain("Native", "half of a pair is never printed alone");
        text.Should().Contain("no upscaler hook ran, and no known upscaler runtime was loaded");
        text.Should().Contain("no frame-generation hook ran, and no known frame-generation runtime was loaded");
        text.Should().NotContainEquivalentOf("frame generation: None");
    }

    [Fact]
    public void AFrameGenerationRuntimeThatIsLoadedMakesTheNumberSuspectByName()
    {
        // (b)/(c) Streamline loaded, the hooks installed, every present UNKNOWN because nothing
        // identifiable ever went through slEvaluateFeature — and nvngx_dlssg.dll is in the
        // process. Whether the title has FG off or drives it through a path this build does not
        // hook is unknowable here, so the number is flagged, and the flag names the module.
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.FgEvaluations),
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.SlDlssG
                                   | FlRuntimeCensus.NvngxDlssG),
        };
        List<FlFrameRecord> stream = Stream(120, FlMeasured.Upscaler | FlMeasured.Fg | FlMeasured.FgCounts,
            upscaler: (byte)FlUpscaler.Unknown, fgMode: (byte)FlFgMode.Unknown);

        string text = Render(stream, writer);

        text.Should().Contain("WARNING: a frame-generation runtime was loaded (sl.dlss_g.dll, nvngx_dlssg.dll)");
        text.Should().Contain("MAY include generated frames");
        text.Should().Contain("upscaling is off in this title's settings, or it runs through a path this build does not hook");
        text.Should().NotContain("our coverage is short");
        text.Should().NotContain("Native FPS");
        text.Should().NotContainEquivalentOf("frame generation: None");
    }

    [Fact]
    public void AnUpscalerRuntimeWithNoHookInstalledIsNamedAsSuch()
    {
        // Streamline 1.5.6 (The Witcher 3): the module is there, ResolveScoped refuses its ABI,
        // no hook installs. The report should say "runtime loaded, no hook", not "no runtime".
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)FlHookFamily.Present,
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer),
        };

        string text = Render(Stream(60), writer);

        text.Should().Contain("no upscaler hook ran, though an upscaler runtime is loaded: sl.interposer.dll");
        text.Should().Contain("cannot include in-process generated frames", "no FG runtime is loaded, only the interposer");
    }

    [Fact]
    public void ACensusThatNeverRanLeavesTheQuestionOpenAndSaysSo()
    {
        var writer = new FlWriterState { HooksInstalledMask = (uint)FlHookFamily.Present };

        string text = Render(Stream(60), writer);

        text.Should().Contain("the runtime census did not run");
        text.Should().NotContain("cannot include", "an absent census proves nothing about absence");
        text.Should().Contain("N/A (no upscaler hook ran)");
    }

    [Fact]
    public void AFamilyBitWithoutRanIsTheWritersDefectAndIsReportedNotRead()
    {
        var writer = new FlWriterState
        {
            HooksInstalledMask = (uint)FlHookFamily.Present,
            RuntimeCensus = (uint)FlRuntimeCensus.NvngxDlssG,
        };

        MeasuredFacts facts = MeasuredFacts.From(Stream(60), writer, Stopwatch.Frequency, 0, 0);
        string text = SessionReport.Render(facts);

        facts.CensusInconsistent.Should().BeTrue();
        facts.CensusRan.Should().BeFalse();
        text.Should().Contain("WARNING: the runtime census names a module while saying it never ran");
        text.Should().NotContain("MAY include generated frames", "a bit the census did not take is not evidence");
    }

    [Fact]
    public void TheFamilyGroupsPartitionEveryNamedBit()
    {
        FlRuntimeCensus all = FlRuntimeCensusFamilies.Fg | FlRuntimeCensusFamilies.Upscaler | FlRuntimeCensus.Ran;

        (FlRuntimeCensusFamilies.Fg & FlRuntimeCensusFamilies.Upscaler).Should().Be(FlRuntimeCensus.None);
        foreach (FlRuntimeCensus bit in Enum.GetValues<FlRuntimeCensus>())
        {
            (all & bit).Should().Be(bit, $"{bit} must belong to exactly one group or be Ran");
        }

        CensusNames.Describe(FlRuntimeCensus.SlDlssG | FlRuntimeCensus.NvngxDlssG).Should().Be("sl.dlss_g.dll, nvngx_dlssg.dll");
        CensusNames.Describe(FlRuntimeCensus.Ran).Should().Be("-");
    }
}
