using System.Globalization;
using System.Text.Json;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Metrics;
using FrameLedger.Application.Persistence;
using FrameLedger.Application.Telemetry;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Recording;

/// <summary>
/// <c>04_CAPTURE</c> §Finalizing steps 2 and 3: segments from resolution changes, then every aggregate of
/// <c>sessions</c> — over Domain's calculators, one mapping from the record, and the ladder in
/// <see cref="FgLadder"/>. Every measured column is nullable and null means "not measured"; nothing here
/// writes a 0 for an answer nobody gave.
/// </summary>
public static class SessionAggregator
{
    /// <summary>Fills <paramref name="skeleton"/>'s measured columns from <paramref name="input"/>.</summary>
    public static AggregationResult Aggregate(SessionRow skeleton, AggregationInput input)
    {
        ArgumentNullException.ThrowIfNull(skeleton);
        ArgumentNullException.ThrowIfNull(input);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(input.QpcFrequency, nameof(input));

        var ctx = new Context(input);
        SessionRow row = ApplyFrames(skeleton, ctx);
        row = ApplyFg(row, ctx);
        row = ApplyUpscaler(row, ctx);
        row = ApplyRt(row, ctx);
        row = ApplyWriter(row, ctx);
        row = ApplySensors(row, ctx);
        row = ApplyWitnesses(row, ctx);

        return new AggregationResult
        {
            Row = row,
            Segments = Segments(ctx),
            Samples = ctx.All,
            Dominant = ctx.Dominant,
            FgVerdict = ctx.Verdict,
            Series = ctx.Series,
        };
    }

    private static SessionRow ApplyFrames(SessionRow row, Context c)
    {
        FrameStatistics stats = c.Stats;
        StutterResult? stutter = c.Series is null ? null : StutterDetector.Detect(c.Series.FrameTimesMs);
        LatencyAggregates latency = LatencyAggregates.From(c.Dominant);
        return row with
        {
            Api = c.Dominant.Count > 0 ? Vocabulary.Api(c.Dominant[0].Api) : null,
            FrameCount = c.Input.Records.Count,
            AppFrameCount = c.Fg is { Refusal: null } fg && c.FgCounted ? fg.Evaluations : c.Input.Records.Count,
            DisplayedFrameCount = c.Fg?.DisplayedPresents ?? c.Input.Records.Count,
            DroppedFrames = 0,
            PresentedFps = c.Series?.AverageFps,
            MedianFps = stats.MedianFps,
            P1LowFps = stats.P1LowFps,
            P01LowFps = stats.P01LowFps,
            MinFps = stats.MinFps,
            MaxFps = stats.MaxFps,
            FrametimeStdDevMs = stats.StdDevMs,
            StutterCount = stutter?.Count,
            StutterTimePct = stutter?.TimePct,
            PsoStutterPct = stutter is null || c.Series is null ? null : StutterDetector.PsoStutterPct(stutter, c.Series, c.Dominant),
            ReflexActive = latency.Count > 0,
            LatencyAvgUs = latency.AverageUs is { } a ? (long)Math.Round(a) : null,
            LatencyP95Us = latency.P95Us is { } p ? (long)Math.Round(p) : null,
            GapCount = c.Input.TotalGaps,
            DroppedRecords = c.Input.TotalDropped,
            DataQualityWarnings = (c.Input.TotalDropped > 0 ? 1 : 0) + (c.Input.TotalGaps > 0 ? 1 : 0),
        };
    }

    private static SessionRow ApplyFg(SessionRow row, Context c)
    {
        FgWindow? fg = c.Fg;
        bool usable = fg is { Refusal: null } && c.Verdict is FgVerdict.Named or FgVerdict.ActiveUnidentified or FgVerdict.None or FgVerdict.NoneInputsTagged;
        return row with
        {
            FgMode = Vocabulary.FgMode(c.Verdict, c.FgIdentity),
            FgSource = Vocabulary.FgSource(c.Verdict),
            FgFactor = usable ? fg!.Factor : null,
            NativeFps = usable ? fg!.NativeFps : null,
            DisplayedFps = usable ? fg!.DisplayedFps : null,
            DisplayedP1LowFps = usable && c.Verdict is FgVerdict.Named or FgVerdict.ActiveUnidentified ? c.Stats.P1LowFps : null,
            DisplayedCountedBy = usable ? (fg!.DxgiCounted ? "dxgi" : "hook") : null,
            FgNoneWithheldReason = c.Withheld,
            PresentedQualifier = PresentedQualifier(c),
            FgDriverReported = c.Input.Ngx.FgCreatedAndEvaluated ? "dlssg" : null,
            FgRuntimeCensus = c.Input.Writer.RuntimeCensus,
            DxgiUnseenTotal = c.Input.Writer.DxgiPresentsUnseen,
            DxgiPresentSamples = c.Input.Writer.DxgiPresentSamples,
            SlTagCensus = c.Input.Writer.SlTagCensus,
            SlInterposerVersion = c.Input.Modules.VersionOf(FgLadder.SlInterposerFileName)?.ToString(),
        };
    }

    /// <summary>The qualifier the one number that stands alone must carry (CLAUDE.md rule 6, as amended).</summary>
    private static string PresentedQualifier(Context c)
    {
        if (c.Withheld is not null)
        {
            return "none_withheld";
        }

        var census = (FlRuntimeCensus)c.Input.Writer.RuntimeCensus;
        if (!census.HasFlag(FlRuntimeCensus.Ran))
        {
            return "census_not_run";
        }

        return (census & FlRuntimeCensusFamilies.Fg) == FlRuntimeCensus.None ? "no_fg_runtime" : "fg_runtime_loaded";
    }

    private static SessionRow ApplyUpscaler(SessionRow row, Context c)
    {
        UpscaleExtent? extent = UpscaleExtent.From(c.Dominant);
        FlUpscaler? identity = FgLadder.UpscalerIdentity(c.Input.Records);
        bool hookRan = FgLadder.UpscalerHookRan(c.Input.Records);
        IReadOnlyList<FrameSample> withParams = [.. c.Dominant.Where(static s => s.Claims(MeasuredFields.UpscalerParams))];
        return row with
        {
            Upscaler = identity is { } u ? Vocabulary.Upscaler(u) : hookRan ? "unknown" : null,
            UpscalerQuality = Modal(withParams, static s => s.UpscalerQuality)?.ToString(CultureInfo.InvariantCulture),
            UpscalerSharpness = Modal(withParams, static s => s.UpscalerSharpness),
            UpscalerDriverReported = c.Input.Ngx.SrCreatedAndEvaluated ? "dlss" : null,
            RenderW = extent?.RenderW,
            RenderH = extent?.RenderH,
            OutputW = extent?.OutputW,
            OutputH = extent?.OutputH,
            UpscaleRatio = extent?.Ratio,
            SettingsChangedMidSession = c.Segments.Count(s => s.Samples.Count > 0) > c.All.Select(static s => s.SwapchainId).Distinct().Count(),
            HdrFlag = Vocabulary.Tri(c.Hdr),
            HdrSource = c.Hdr == Tri.NotApplicable ? null : Vocabulary.Measured,
        };
    }

    private static SessionRow ApplyRt(SessionRow row, Context c)
    {
        Tri rt = RtVerdict.RayTracing(c.Dominant, c.Facts);
        Tri rr = RtVerdict.RayReconstruction(c.Dominant);
        RtSummary summary = RtVerdict.Summarise(c.Dominant);
        var hooks = (FlHookFamily)c.Input.Writer.HooksInstalledMask;
        return row with
        {
            RtFlag = Vocabulary.Tri(rt),
            RtSource = rt == Tri.NotApplicable ? null : Vocabulary.Measured,
            RrFlag = Vocabulary.Tri(rr),
            RrSource = rr == Tri.NotApplicable ? null : Vocabulary.Measured,
            PtFlag = Vocabulary.NotApplicable,
            RtFramePct = summary.FramePct,
            RaysPerPixel = summary.RaysPerPixel,
            RtPsoCount = hooks.HasFlag(FlHookFamily.RtPso) ? c.Input.Writer.RtStateObjectsCreated : null,
            RtTier = c.Input.Writer.RtTier == (uint)RtTierValue.NotQueried ? null : c.Input.Writer.RtTier,
            HooksInstalledMask = c.Input.Writer.HooksInstalledMask,
            RasterPsoCount = hooks.HasFlag(FlHookFamily.Pso) ? c.Input.Writer.RasterPsoCreated : null,
        };
    }

    private static SessionRow ApplyWriter(SessionRow row, Context c)
    {
        FlWriterState w = c.Input.Writer;
        VramAggregates vram = VramAggregates.From(c.Dominant, c.Facts.VramBudgetMb);
        return row with
        {
            FaultCount = w.FaultCount,
            WriterStatusAtEnd = w.Status,
            EarlyStopFamily = w.EarlyStopFamily,
            LoaderSignals = w.LoaderSignals,
            DxgiPresentsBeforeHook = c.Facts.DxgiPresentsBeforeHook,
            VramProcAvgMb = vram.AverageMb,
            VramProcMaxMb = vram.MaxMb,
            VramBudgetExceededPct = vram.BudgetExceededPct,
        };
    }

    private static SessionRow ApplySensors(SessionRow row, Context c)
    {
        IReadOnlyList<GpuSample> s = [.. c.Input.Sensors.Select(static t => t.Sample)];
        SeriesAggregates temp = SeriesAggregates.Of(s.Select(static x => x.TempCoreC));
        SeriesAggregates hotspot = SeriesAggregates.Of(s.Select(static x => x.TempHotspotC));
        SeriesAggregates load = SeriesAggregates.Of(s.Select(static x => x.LoadPct));
        SeriesAggregates power = SeriesAggregates.Of(s.Select(static x => x.PowerW));
        SeriesAggregates adapter = SeriesAggregates.Of(s.Select(static x => x.VramAdapterMb));
        int throttleSamples = s.Count(static x => x.ThrottleReasons is not null);
        return row with
        {
            AvgGpuTemp = temp.Average,
            MaxGpuTemp = temp.Max,
            MaxGpuHotspot = hotspot.Max,
            AvgGpuLoad = load.Average,
            AvgGpuPowerW = power.Average,
            VramAdapterMaxMb = adapter.Max,
            ThrottlePct = throttleSamples == 0 ? null : 100.0 * s.Count(static x => x.ThrottleReasons is > 0) / throttleSamples,
        };
    }

    private static SessionRow ApplyWitnesses(SessionRow row, Context c)
    {
        NgxDriverState ngx = c.Input.Ngx;
        return row with
        {
            RuntimeModulesJson = c.Input.Modules.Snapshots == 0 ? null : JsonSerializer.Serialize(
                c.Input.Modules.Modules.ToDictionary(static m => m.FileName, static m => m.FileVersion, StringComparer.OrdinalIgnoreCase),
                RecordingJsonContext.Default.DictionaryStringString),
            ExecutableMarkersJson = !c.Input.Markers.Scanned ? null : JsonSerializer.Serialize(
                c.Input.Markers.Markers.ToDictionary(static m => m.Name, static m => m.Hits, StringComparer.Ordinal),
                RecordingJsonContext.Default.DictionaryStringInt32),
            NgxDriverWordsJson = ngx.Outcome == NgxProbeOutcome.NotRun ? null : JsonSerializer.Serialize(new NgxDriverWords
            {
                Outcome = ngx.Outcome.ToString(),
                Sr = ngx.Outcome == NgxProbeOutcome.Answered ? "0x" + ngx.Sr.ToString("X", CultureInfo.InvariantCulture) : null,
                Rr = ngx.Outcome == NgxProbeOutcome.Answered ? "0x" + ngx.Rr.ToString("X", CultureInfo.InvariantCulture) : null,
                Fg = ngx.Outcome == NgxProbeOutcome.Answered ? "0x" + ngx.Fg.ToString("X", CultureInfo.InvariantCulture) : null,
                Driver = ngx.Outcome == NgxProbeOutcome.Answered ? ngx.Driver : null,
                Readings = ngx.Readings,
                Answered = ngx.Answered,
                Changed = ngx.Changed,
                Detail = ngx.Detail,
            }, RecordingJsonContext.Default.NgxDriverWords),
        };
    }

    private static List<SegmentRow> Segments(Context c)
    {
        var rows = new List<SegmentRow>(c.Segments.Count);
        foreach (Segment seg in c.Segments)
        {
            if (seg.Samples.Count == 0)
            {
                continue;
            }

            FrameTimeSeries series = FrameTimeSeries.From(seg.Samples, null, c.Input.QpcFrequency);
            FrameStatistics stats = FrameStatistics.From(series.FrameTimesMs, presented: true);
            UpscaleExtent? extent = UpscaleExtent.From(seg.Samples);
            UpscalerKind? upscaler = seg.Samples.Where(static s => s.Claims(MeasuredFields.Upscaler))
                .Select(static s => s.Upscaler)
                .FirstOrDefault(static u => u is not UpscalerKind.NotReported and not UpscalerKind.Unknown and not UpscalerKind.RetiredRayReconstruction, UpscalerKind.NotReported);
            rows.Add(new SegmentRow
            {
                SwapchainId = seg.SwapchainId,
                StartFrame = seg.Samples[0].FrameIndex,
                EndFrame = seg.Samples[^1].FrameIndex,
                RenderW = extent?.RenderW,
                RenderH = extent?.RenderH,
                OutputW = seg.OutputW == 0 ? null : seg.OutputW,
                OutputH = seg.OutputH == 0 ? null : seg.OutputH,
                Upscaler = upscaler is { } u && u != UpscalerKind.NotReported ? Vocabulary.Upscaler(u) : null,
                UpscalerQuality = Modal([.. seg.Samples.Where(static s => s.Claims(MeasuredFields.UpscalerParams))], static s => s.UpscalerQuality)
                    ?.ToString(CultureInfo.InvariantCulture),
                FgMode = null,
                NativeFps = null,
                DisplayedFps = series.AverageFps,
                P1LowFps = stats.P1LowFps,
            });
        }

        return rows;
    }

    private static int? Modal(IReadOnlyList<FrameSample> samples, Func<FrameSample, byte> field) =>
        samples.Count == 0 ? null : samples.GroupBy(field).OrderByDescending(static g => g.Count()).ThenBy(static g => g.Key).First().Key;

    /// <summary>The intermediates every Apply step reads, computed once.</summary>
    private sealed class Context
    {
        public Context(AggregationInput input)
        {
            Input = input;
            All = FrameSampleMapper.Map(input.Records);
            Facts = FrameSampleMapper.Map(input.Writer);
            IReadOnlyList<FlFrameRecord> dominantRecords = SegmentBuilder.DominantStream(input.Records, static r => r.SwapchainId);
            Dominant = FrameSampleMapper.Map(dominantRecords);
            Segments = SegmentBuilder.Build(All);

            HashSet<int> gapBefore = DominantGaps(input, dominantRecords);
            Series = Dominant.Count > 0 ? FrameTimeSeries.From(Dominant, gapBefore, input.QpcFrequency) : null;

            Fg = All.Count > 0 ? FgWindow.From(All, input.QpcFrequency) : null;
            FgCounted = FgLadder.FgHookRan(input.Records);
            var census = (FlRuntimeCensus)input.Writer.RuntimeCensus;
            Withheld = Fg?.IsNone == true ? FgLadder.WithholdNone(census, input.Modules, input.Writer) : null;
            FgIdentity = FgLadder.Identity(dominantRecords);
            Verdict = FgLadder.Resolve(FgIdentity, Fg, Withheld);
            Stats = Series is null
                ? FrameStatistics.Empty(presented: true)
                : FrameStatistics.From(Series.FrameTimesMs, presented: Verdict is not (FgVerdict.Named or FgVerdict.ActiveUnidentified));
            Hdr = HdrVerdict.Of(Dominant);
        }

        public AggregationInput Input { get; }

        public IReadOnlyList<FrameSample> All { get; }

        public IReadOnlyList<FrameSample> Dominant { get; }

        public WriterFacts Facts { get; }

        public IReadOnlyList<Segment> Segments { get; }

        public FrameTimeSeries? Series { get; }

        public FgWindow? Fg { get; }

        public bool FgCounted { get; }

        public string? Withheld { get; }

        public FlFgMode? FgIdentity { get; }

        public FgVerdict Verdict { get; }

        public FrameStatistics Stats { get; }

        public Tri Hdr { get; }

        /// <summary>The gap-before set re-indexed onto the dominant stream: a gap in the drained order lands on the same record there.</summary>
        private static HashSet<int> DominantGaps(AggregationInput input, IReadOnlyList<FlFrameRecord> dominant)
        {
            var result = new HashSet<int>();
            if (input.GapBefore.Count == 0 || dominant.Count == 0)
            {
                return result;
            }

            uint id = dominant[0].SwapchainId;
            var gaps = new HashSet<int>(input.GapBefore);
            int k = 0;
            for (int i = 0; i < input.Records.Count; i++)
            {
                if (input.Records[i].SwapchainId != id)
                {
                    continue;
                }

                if (gaps.Contains(i))
                {
                    result.Add(k);
                }

                k++;
            }

            return result;
        }
    }
}
