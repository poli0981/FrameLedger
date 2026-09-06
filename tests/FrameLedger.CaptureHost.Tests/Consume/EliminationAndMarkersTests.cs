using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// Rung 3 by elimination, and the executable's markers as the clear census's second witness (<c>HANDOFF</c> 7b,
/// §H11): every sentence names what excluded what, and none names a vendor as measured.
/// </summary>
public sealed class EliminationAndMarkersTests
{
    private const FlHookFamily _hooks =
        FlHookFamily.Present | FlHookFamily.UpscalerIdentity | FlHookFamily.UpscalerParams | FlHookFamily.FgEvaluations;

    private static readonly NgxDriverState _fgNotCreated = NgxDriverState.Parse(
        "NGXSTATE status=ANSWERED sr=0x5 rr=0x5 fg=0x5 ratio=0.0000 mode=0 preset=0 fgcount=0 fgpreset=0 fgmode=0 driver=61664");

    private static ExecutableMarkers Markers(params (string Name, bool Fg, int Hits)[] found) => new(
        [.. found.Select(f => new ExecutableMarker(f.Name, "test", f.Fg, f.Hits))], BytesScanned: 1_000_000, Error: null);

    /// <summary>k presents per application frame, the token on the first, no tag naming a technology.</summary>
    private static List<FlFrameRecord> Stream(int appFrames, int k, bool counted = true)
    {
        List<FlFrameRecord> stream = [];
        ulong qpc = 1_000_000;
        ulong step = (ulong)(Stopwatch.Frequency / (60 * k));
        for (int f = 0; f < appFrames; f++)
        {
            for (int p = 0; p < k; p++)
            {
                stream.Add(new FlFrameRecord
                {
                    Qpc = qpc,
                    SwapchainId = 1,
                    OutputW = 2560,
                    OutputH = 1440,
                    MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler
                                            | (counted ? FlMeasured.Fg | FlMeasured.FgCounts : FlMeasured.None)),
                    FgEvaluations = (byte)(counted && p == 0 ? 1 : 0),
                    FgMode = (byte)FlFgMode.Unknown,
                });
                qpc += step;
            }
        }

        return stream;
    }

    private static (MeasuredFacts Facts, string Text) Render(List<FlFrameRecord> stream, FlRuntimeCensus census,
        NgxDriverState? ngx, ExecutableMarkers? markers, uint tagCensus = 0)
    {
        var writer = new FlWriterState { HooksInstalledMask = (uint)_hooks, RuntimeCensus = (uint)census, SlTagCensus = tagCensus };
        MeasuredFacts facts = MeasuredFacts.From(stream, writer, Stopwatch.Frequency, 0, 0,
            FgWindow.From(stream, Stopwatch.Frequency), ngx: ngx, markers: markers);
        return (facts, SessionReport.Render(facts));
    }

    [Fact]
    public void AnXeFgShapeIsActiveByEliminationNamingTheRuntimeLoadedAndNeverXeFg()
    {
        // The expected XeFG session: counted ×2 by the token, the driver's FG word clear, no tag,
        // no FSR dispatch, libxess_fg.dll the only frame-generation runtime in the census.
        FlRuntimeCensus census = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.LibXess | FlRuntimeCensus.LibXessFg;

        (MeasuredFacts facts, string text) = Render(Stream(100, 2), census, _fgNotCreated, Markers());

        facts.FgMode.Should().Be(MeasuredFacts.FgActiveUnidentified);
        facts.FgModePrinted.Should().StartWith(MeasuredFacts.FgActiveUnidentified + " — by elimination among what this session saw: ");
        text.Should().Contain("(x2 FG)");
        text.Should().Contain("not DLSS-G (the NVIDIA driver reports no NGX frame-generation feature created, and no HUD-less / UI tag was sent through Streamline)");
        text.Should().Contain("no FSR frame-generation dispatch reached a hooked module");
        text.Should().Contain("the frame-generation runtime(s) loaded: libxess_fg.dll");
        text.Should().Contain("a frame generator compiled into the executable would read the same");
        text.Should().NotContain("XeFg").And.NotContain("frame generation: XeSS");
    }

    [Fact]
    public void TheCompiledInShapeSaysNoRuntimeIsLoadedAndNamesTheExecutablesMarkers()
    {
        // Rune Factory at FSR FG: the UE title requests the Streamline token regardless, the census has
        // no frame-generation module, and the executable carries the FSR 3 strings.
        FlRuntimeCensus census = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer;

        (_, string text) = Render(Stream(100, 2), census, _fgNotCreated, Markers(("ffxFsr3", true, 7), ("FidelityFX", false, 3)));

        text.Should().Contain("no frame-generation runtime module is loaded at all; the executable itself carries ffxFsr3");
        text.Should().Contain("(x2 FG)");
    }

    [Fact]
    public void WithoutTheDriversAnswerDlssGIsNotExcludedAndTheLineSaysSo()
    {
        FlRuntimeCensus census = FlRuntimeCensus.Ran | FlRuntimeCensus.SlInterposer | FlRuntimeCensus.LibXessFg;

        (_, string text) = Render(Stream(100, 2), census, null, null, tagCensus: (uint)FlSlTagType.Hudless << FlSlTagRoute.Global);

        text.Should().Contain("DLSS-G not excluded (the NVIDIA driver did not answer; HUD-less / UI tags WERE sent through Streamline)");
    }

    [Fact]
    public void AClearCensusWithAnFgCapableMarkerInTheFileSaysMayNotCannot()
    {
        // Token-less and compiled-in: nothing counted, no module, but the file carries the FSR 3 strings.
        (_, string text) = Render(Stream(200, 1, counted: false), FlRuntimeCensus.Ran, null, Markers(("ffxFrameInterpolation", true, 2)));

        text.Should().Contain("Presented FPS:");
        text.Should().Contain("WARNING: no known frame-generation runtime MODULE was loaded, but the executable itself carries ffxFrameInterpolation");
        text.Should().Contain("MAY include generated frames");
        text.Should().NotContain("cannot include in-process generated frames");
    }

    [Fact]
    public void AClearCensusAndACleanFileKeepsCannotWithTheFileNamedAndAnUnscannedFileIsByteIdentical()
    {
        (_, string scanned) = Render(Stream(200, 1, counted: false), FlRuntimeCensus.Ran, null, Markers(("NVSDK_NGX", false, 4)));
        (_, string unscanned) = Render(Stream(200, 1, counted: false), FlRuntimeCensus.Ran, null, null);

        scanned.Should().Contain("cannot include in-process generated frames")
            .And.Contain("the executable carries none of the frame-generation SDK strings this host scans for");
        unscanned.Should().Contain("cannot include in-process generated frames (statically linked FSR3-FG and driver-level AFMF are outside what this can see)");
        unscanned.Should().NotContain("executable carries");
    }
}
