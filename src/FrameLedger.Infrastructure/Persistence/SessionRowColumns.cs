using System.Data.Common;
using FrameLedger.Application.Persistence;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// The <c>sessions</c> row's column map, GENERATED from one table so the INSERT, the parameter bag and the
/// reader cannot drift from each other. Regenerate with <c>gen_session_repo.py</c> (in the PR that adds a
/// column) rather than editing by hand; the chunking is the analyzer's 60-line method limit, nothing more.
/// </summary>
internal static class SessionRowColumns
{
    public const string Insert =
        "INSERT INTO sessions (session_guid, game_id, snapshot_id, started_at, ended_at, duration_s, qpc_epoch, qpc_frequency, capture_tier, capture_mode, capture_notes, late_attach, telemetry_source, overlay_build_id, exit_status, drain_ticks, foreground_ticks, records_before_attach, dxgi_presents_before_hook, gap_count, guard_ticks_published, launch_wait_ms, api, present_mode, swap_effect, hdr_flag, hdr_source, sync_interval_mode, upscaler, upscaler_quality, upscaler_sharpness, upscaler_driver_reported, render_w, render_h, output_w, output_h, upscale_ratio, settings_changed_midsession, fg_mode, fg_source, fg_factor, fg_driver_reported, fg_runtime_census, fg_none_withheld_reason, presented_fps, presented_qualifier, dxgi_unseen_total, dxgi_present_samples, displayed_counted_by, sl_tag_census, sl_interposer_version, runtime_modules, executable_markers, ngx_driver_words, rt_flag, rt_source, pt_flag, pt_source, pt_confidence, rr_flag, rr_source, rt_frame_pct, rays_per_pixel, rt_pso_count, rt_tier, hooks_installed_mask, raster_pso_count, frame_count, app_frame_count, displayed_frame_count, dropped_frames, native_fps, displayed_fps, median_fps, p1_low_fps, p01_low_fps, displayed_p1_low_fps, min_fps, max_fps, frametime_stddev_ms, stutter_count, stutter_time_pct, pso_stutter_pct, reflex_active, latency_avg_us, latency_p95_us, dropped_records, fault_count, data_quality_warnings, writer_status_at_end, early_stop_family, loader_signals, vram_proc_avg_mb, vram_proc_max_mb, vram_budget_exceeded_pct, vram_adapter_max_mb, avg_cpu_temp, max_cpu_temp, avg_gpu_temp, max_gpu_temp, max_gpu_hotspot, avg_gpu_load, avg_cpu_load, avg_ram_mb, avg_gpu_power_w, throttle_pct) "
        + "VALUES (@session_guid, @game_id, @snapshot_id, @started_at, @ended_at, @duration_s, @qpc_epoch, @qpc_frequency, @capture_tier, @capture_mode, @capture_notes, @late_attach, @telemetry_source, @overlay_build_id, @exit_status, @drain_ticks, @foreground_ticks, @records_before_attach, @dxgi_presents_before_hook, @gap_count, @guard_ticks_published, @launch_wait_ms, @api, @present_mode, @swap_effect, @hdr_flag, @hdr_source, @sync_interval_mode, @upscaler, @upscaler_quality, @upscaler_sharpness, @upscaler_driver_reported, @render_w, @render_h, @output_w, @output_h, @upscale_ratio, @settings_changed_midsession, @fg_mode, @fg_source, @fg_factor, @fg_driver_reported, @fg_runtime_census, @fg_none_withheld_reason, @presented_fps, @presented_qualifier, @dxgi_unseen_total, @dxgi_present_samples, @displayed_counted_by, @sl_tag_census, @sl_interposer_version, @runtime_modules, @executable_markers, @ngx_driver_words, @rt_flag, @rt_source, @pt_flag, @pt_source, @pt_confidence, @rr_flag, @rr_source, @rt_frame_pct, @rays_per_pixel, @rt_pso_count, @rt_tier, @hooks_installed_mask, @raster_pso_count, @frame_count, @app_frame_count, @displayed_frame_count, @dropped_frames, @native_fps, @displayed_fps, @median_fps, @p1_low_fps, @p01_low_fps, @displayed_p1_low_fps, @min_fps, @max_fps, @frametime_stddev_ms, @stutter_count, @stutter_time_pct, @pso_stutter_pct, @reflex_active, @latency_avg_us, @latency_p95_us, @dropped_records, @fault_count, @data_quality_warnings, @writer_status_at_end, @early_stop_family, @loader_signals, @vram_proc_avg_mb, @vram_proc_max_mb, @vram_budget_exceeded_pct, @vram_adapter_max_mb, @avg_cpu_temp, @max_cpu_temp, @avg_gpu_temp, @max_gpu_temp, @max_gpu_hotspot, @avg_gpu_load, @avg_cpu_load, @avg_ram_mb, @avg_gpu_power_w, @throttle_pct) RETURNING id";

    public const string Select = "SELECT id, session_guid, game_id, snapshot_id, started_at, ended_at, duration_s, qpc_epoch, qpc_frequency, capture_tier, capture_mode, capture_notes, late_attach, telemetry_source, overlay_build_id, exit_status, drain_ticks, foreground_ticks, records_before_attach, dxgi_presents_before_hook, gap_count, guard_ticks_published, launch_wait_ms, api, present_mode, swap_effect, hdr_flag, hdr_source, sync_interval_mode, upscaler, upscaler_quality, upscaler_sharpness, upscaler_driver_reported, render_w, render_h, output_w, output_h, upscale_ratio, settings_changed_midsession, fg_mode, fg_source, fg_factor, fg_driver_reported, fg_runtime_census, fg_none_withheld_reason, presented_fps, presented_qualifier, dxgi_unseen_total, dxgi_present_samples, displayed_counted_by, sl_tag_census, sl_interposer_version, runtime_modules, executable_markers, ngx_driver_words, rt_flag, rt_source, pt_flag, pt_source, pt_confidence, rr_flag, rr_source, rt_frame_pct, rays_per_pixel, rt_pso_count, rt_tier, hooks_installed_mask, raster_pso_count, frame_count, app_frame_count, displayed_frame_count, dropped_frames, native_fps, displayed_fps, median_fps, p1_low_fps, p01_low_fps, displayed_p1_low_fps, min_fps, max_fps, frametime_stddev_ms, stutter_count, stutter_time_pct, pso_stutter_pct, reflex_active, latency_avg_us, latency_p95_us, dropped_records, fault_count, data_quality_warnings, writer_status_at_end, early_stop_family, loader_signals, vram_proc_avg_mb, vram_proc_max_mb, vram_budget_exceeded_pct, vram_adapter_max_mb, avg_cpu_temp, max_cpu_temp, avg_gpu_temp, max_gpu_temp, max_gpu_hotspot, avg_gpu_load, avg_cpu_load, avg_ram_mb, avg_gpu_power_w, throttle_pct FROM sessions";

    public static Dictionary<string, object?> Parameters(SessionRow s)
    {
        var d = new Dictionary<string, object?>(StringComparer.Ordinal);
        Parameters0(d, s);
        Parameters1(d, s);
        Parameters2(d, s);
        Parameters3(d, s);
        return d;
    }

    public static SessionRow Read(DbDataReader r) => Read3(Read2(Read1(Read0(r), r), r), r);


    private static void Parameters0(Dictionary<string, object?> d, SessionRow s)
    {
        d["session_guid"] = s.SessionGuid.ToString("D");
        d["game_id"] = s.GameId;
        d["snapshot_id"] = s.SnapshotId;
        d["started_at"] = s.StartedAt.ToUnixTimeMilliseconds();
        d["ended_at"] = s.EndedAt.ToUnixTimeMilliseconds();
        d["duration_s"] = s.DurationSeconds;
        d["qpc_epoch"] = unchecked((long)s.QpcEpoch);
        d["qpc_frequency"] = s.QpcFrequency;
        d["capture_tier"] = (long)s.Tier;
        d["capture_mode"] = s.Mode == CaptureMode.Launch ? "launch" : "attach";
        d["capture_notes"] = s.CaptureNotes;
        d["late_attach"] = s.LateAttach ? 1L : 0L;
        d["telemetry_source"] = s.TelemetrySource;
        d["overlay_build_id"] = s.OverlayBuildId;
        d["exit_status"] = ExitStatusText(s.ExitStatus);
        d["drain_ticks"] = s.DrainTicks;
        d["foreground_ticks"] = s.ForegroundTicks;
        d["records_before_attach"] = s.RecordsBeforeAttach;
        d["dxgi_presents_before_hook"] = s.DxgiPresentsBeforeHook;
        d["gap_count"] = s.GapCount;
        d["guard_ticks_published"] = s.GuardTicksPublished;
        d["launch_wait_ms"] = s.LaunchWaitMs;
        d["api"] = s.Api;
        d["present_mode"] = s.PresentMode;
        d["swap_effect"] = s.SwapEffect;
        d["hdr_flag"] = s.HdrFlag;
        d["hdr_source"] = s.HdrSource;
    }

    private static void Parameters1(Dictionary<string, object?> d, SessionRow s)
    {
        d["sync_interval_mode"] = s.SyncIntervalMode;
        d["upscaler"] = s.Upscaler;
        d["upscaler_quality"] = s.UpscalerQuality;
        d["upscaler_sharpness"] = s.UpscalerSharpness;
        d["upscaler_driver_reported"] = s.UpscalerDriverReported;
        d["render_w"] = s.RenderW;
        d["render_h"] = s.RenderH;
        d["output_w"] = s.OutputW;
        d["output_h"] = s.OutputH;
        d["upscale_ratio"] = s.UpscaleRatio;
        d["settings_changed_midsession"] = s.SettingsChangedMidSession ? 1L : 0L;
        d["fg_mode"] = s.FgMode;
        d["fg_source"] = s.FgSource;
        d["fg_factor"] = s.FgFactor;
        d["fg_driver_reported"] = s.FgDriverReported;
        d["fg_runtime_census"] = s.FgRuntimeCensus;
        d["fg_none_withheld_reason"] = s.FgNoneWithheldReason;
        d["presented_fps"] = s.PresentedFps;
        d["presented_qualifier"] = s.PresentedQualifier;
        d["dxgi_unseen_total"] = s.DxgiUnseenTotal;
        d["dxgi_present_samples"] = s.DxgiPresentSamples;
        d["displayed_counted_by"] = s.DisplayedCountedBy;
        d["sl_tag_census"] = s.SlTagCensus;
        d["sl_interposer_version"] = s.SlInterposerVersion;
        d["runtime_modules"] = s.RuntimeModulesJson;
        d["executable_markers"] = s.ExecutableMarkersJson;
        d["ngx_driver_words"] = s.NgxDriverWordsJson;
    }

    private static void Parameters2(Dictionary<string, object?> d, SessionRow s)
    {
        d["rt_flag"] = s.RtFlag;
        d["rt_source"] = s.RtSource;
        d["pt_flag"] = s.PtFlag;
        d["pt_source"] = s.PtSource;
        d["pt_confidence"] = s.PtConfidence;
        d["rr_flag"] = s.RrFlag;
        d["rr_source"] = s.RrSource;
        d["rt_frame_pct"] = s.RtFramePct;
        d["rays_per_pixel"] = s.RaysPerPixel;
        d["rt_pso_count"] = s.RtPsoCount;
        d["rt_tier"] = s.RtTier;
        d["hooks_installed_mask"] = s.HooksInstalledMask;
        d["raster_pso_count"] = s.RasterPsoCount;
        d["frame_count"] = s.FrameCount;
        d["app_frame_count"] = s.AppFrameCount;
        d["displayed_frame_count"] = s.DisplayedFrameCount;
        d["dropped_frames"] = s.DroppedFrames;
        d["native_fps"] = s.NativeFps;
        d["displayed_fps"] = s.DisplayedFps;
        d["median_fps"] = s.MedianFps;
        d["p1_low_fps"] = s.P1LowFps;
        d["p01_low_fps"] = s.P01LowFps;
        d["displayed_p1_low_fps"] = s.DisplayedP1LowFps;
        d["min_fps"] = s.MinFps;
        d["max_fps"] = s.MaxFps;
        d["frametime_stddev_ms"] = s.FrametimeStdDevMs;
        d["stutter_count"] = s.StutterCount;
    }

    private static void Parameters3(Dictionary<string, object?> d, SessionRow s)
    {
        d["stutter_time_pct"] = s.StutterTimePct;
        d["pso_stutter_pct"] = s.PsoStutterPct;
        d["reflex_active"] = s.ReflexActive is { } reflex ? (reflex ? 1L : 0L) : null;
        d["latency_avg_us"] = s.LatencyAvgUs;
        d["latency_p95_us"] = s.LatencyP95Us;
        d["dropped_records"] = s.DroppedRecords;
        d["fault_count"] = s.FaultCount;
        d["data_quality_warnings"] = s.DataQualityWarnings;
        d["writer_status_at_end"] = s.WriterStatusAtEnd;
        d["early_stop_family"] = s.EarlyStopFamily;
        d["loader_signals"] = s.LoaderSignals;
        d["vram_proc_avg_mb"] = s.VramProcAvgMb;
        d["vram_proc_max_mb"] = s.VramProcMaxMb;
        d["vram_budget_exceeded_pct"] = s.VramBudgetExceededPct;
        d["vram_adapter_max_mb"] = s.VramAdapterMaxMb;
        d["avg_cpu_temp"] = s.AvgCpuTemp;
        d["max_cpu_temp"] = s.MaxCpuTemp;
        d["avg_gpu_temp"] = s.AvgGpuTemp;
        d["max_gpu_temp"] = s.MaxGpuTemp;
        d["max_gpu_hotspot"] = s.MaxGpuHotspot;
        d["avg_gpu_load"] = s.AvgGpuLoad;
        d["avg_cpu_load"] = s.AvgCpuLoad;
        d["avg_ram_mb"] = s.AvgRamMb;
        d["avg_gpu_power_w"] = s.AvgGpuPowerW;
        d["throttle_pct"] = s.ThrottlePct;
    }


    private static SessionRow Read0(DbDataReader r) => new()
    {
        Id = L(r, "id")!.Value,
        SessionGuid = Guid.Parse(S(r, "session_guid")!),
        GameId = L(r, "game_id")!.Value,
        SnapshotId = L(r, "snapshot_id")!.Value,
        StartedAt = DateTimeOffset.FromUnixTimeMilliseconds(L(r, "started_at")!.Value),
        EndedAt = DateTimeOffset.FromUnixTimeMilliseconds(L(r, "ended_at")!.Value),
        QpcEpoch = unchecked((ulong)L(r, "qpc_epoch")!.Value),
        QpcFrequency = L(r, "qpc_frequency")!.Value,
        Tier = (CaptureTier)L(r, "capture_tier")!.Value,
        Mode = string.Equals(S(r, "capture_mode"), "launch", StringComparison.Ordinal) ? CaptureMode.Launch : CaptureMode.Attach,
        CaptureNotes = S(r, "capture_notes"),
        LateAttach = L(r, "late_attach") == 1,
        TelemetrySource = S(r, "telemetry_source"),
        OverlayBuildId = S(r, "overlay_build_id"),
        ExitStatus = ParseExitStatus(S(r, "exit_status")!),
        DrainTicks = L(r, "drain_ticks"),
        ForegroundTicks = L(r, "foreground_ticks"),
        RecordsBeforeAttach = L(r, "records_before_attach"),
        DxgiPresentsBeforeHook = L(r, "dxgi_presents_before_hook"),
        GapCount = L(r, "gap_count")!.Value,
        GuardTicksPublished = L(r, "guard_ticks_published"),
        LaunchWaitMs = L(r, "launch_wait_ms"),
        Api = S(r, "api"),
        PresentMode = S(r, "present_mode"),
        SwapEffect = S(r, "swap_effect"),
        HdrFlag = S(r, "hdr_flag")!,
        HdrSource = S(r, "hdr_source"),
    };

    private static SessionRow Read1(SessionRow row, DbDataReader r) => row with
    {
        SyncIntervalMode = S(r, "sync_interval_mode"),
        Upscaler = S(r, "upscaler"),
        UpscalerQuality = S(r, "upscaler_quality"),
        UpscalerSharpness = I(r, "upscaler_sharpness"),
        UpscalerDriverReported = S(r, "upscaler_driver_reported"),
        RenderW = I(r, "render_w"),
        RenderH = I(r, "render_h"),
        OutputW = I(r, "output_w"),
        OutputH = I(r, "output_h"),
        UpscaleRatio = D(r, "upscale_ratio"),
        SettingsChangedMidSession = L(r, "settings_changed_midsession") == 1,
        FgMode = S(r, "fg_mode")!,
        FgSource = S(r, "fg_source"),
        FgFactor = D(r, "fg_factor"),
        FgDriverReported = S(r, "fg_driver_reported"),
        FgRuntimeCensus = L(r, "fg_runtime_census"),
        FgNoneWithheldReason = S(r, "fg_none_withheld_reason"),
        PresentedFps = D(r, "presented_fps"),
        PresentedQualifier = S(r, "presented_qualifier"),
        DxgiUnseenTotal = L(r, "dxgi_unseen_total"),
        DxgiPresentSamples = L(r, "dxgi_present_samples"),
        DisplayedCountedBy = S(r, "displayed_counted_by"),
        SlTagCensus = L(r, "sl_tag_census"),
        SlInterposerVersion = S(r, "sl_interposer_version"),
        RuntimeModulesJson = S(r, "runtime_modules"),
        ExecutableMarkersJson = S(r, "executable_markers"),
        NgxDriverWordsJson = S(r, "ngx_driver_words"),
    };

    private static SessionRow Read2(SessionRow row, DbDataReader r) => row with
    {
        RtFlag = S(r, "rt_flag")!,
        RtSource = S(r, "rt_source"),
        PtFlag = S(r, "pt_flag")!,
        PtSource = S(r, "pt_source"),
        PtConfidence = D(r, "pt_confidence"),
        RrFlag = S(r, "rr_flag")!,
        RrSource = S(r, "rr_source"),
        RtFramePct = D(r, "rt_frame_pct"),
        RaysPerPixel = D(r, "rays_per_pixel"),
        RtPsoCount = L(r, "rt_pso_count"),
        RtTier = L(r, "rt_tier"),
        HooksInstalledMask = L(r, "hooks_installed_mask"),
        RasterPsoCount = L(r, "raster_pso_count"),
        FrameCount = L(r, "frame_count")!.Value,
        AppFrameCount = L(r, "app_frame_count")!.Value,
        DisplayedFrameCount = L(r, "displayed_frame_count")!.Value,
        DroppedFrames = L(r, "dropped_frames")!.Value,
        NativeFps = D(r, "native_fps"),
        DisplayedFps = D(r, "displayed_fps"),
        MedianFps = D(r, "median_fps"),
        P1LowFps = D(r, "p1_low_fps"),
        P01LowFps = D(r, "p01_low_fps"),
        DisplayedP1LowFps = D(r, "displayed_p1_low_fps"),
        MinFps = D(r, "min_fps"),
        MaxFps = D(r, "max_fps"),
        FrametimeStdDevMs = D(r, "frametime_stddev_ms"),
        StutterCount = L(r, "stutter_count"),
    };

    private static SessionRow Read3(SessionRow row, DbDataReader r) => row with
    {
        StutterTimePct = D(r, "stutter_time_pct"),
        PsoStutterPct = D(r, "pso_stutter_pct"),
        ReflexActive = L(r, "reflex_active") is { } reflex ? reflex != 0 : null,
        LatencyAvgUs = L(r, "latency_avg_us"),
        LatencyP95Us = L(r, "latency_p95_us"),
        DroppedRecords = L(r, "dropped_records")!.Value,
        FaultCount = L(r, "fault_count")!.Value,
        DataQualityWarnings = L(r, "data_quality_warnings")!.Value,
        WriterStatusAtEnd = L(r, "writer_status_at_end"),
        EarlyStopFamily = L(r, "early_stop_family"),
        LoaderSignals = L(r, "loader_signals"),
        VramProcAvgMb = D(r, "vram_proc_avg_mb"),
        VramProcMaxMb = D(r, "vram_proc_max_mb"),
        VramBudgetExceededPct = D(r, "vram_budget_exceeded_pct"),
        VramAdapterMaxMb = D(r, "vram_adapter_max_mb"),
        AvgCpuTemp = D(r, "avg_cpu_temp"),
        MaxCpuTemp = D(r, "max_cpu_temp"),
        AvgGpuTemp = D(r, "avg_gpu_temp"),
        MaxGpuTemp = D(r, "max_gpu_temp"),
        MaxGpuHotspot = D(r, "max_gpu_hotspot"),
        AvgGpuLoad = D(r, "avg_gpu_load"),
        AvgCpuLoad = D(r, "avg_cpu_load"),
        AvgRamMb = D(r, "avg_ram_mb"),
        AvgGpuPowerW = D(r, "avg_gpu_power_w"),
        ThrottlePct = D(r, "throttle_pct"),
    };

    public static string ExitStatusText(ExitStatus status) => status switch
    {
        ExitStatus.Normal => "normal",
        ExitStatus.Crashed => "crashed",
        ExitStatus.UnhookedSafety => "unhooked_safety",
        ExitStatus.Degraded => "degraded",
        ExitStatus.Interrupted => "interrupted",
        _ => throw new ArgumentOutOfRangeException(nameof(status), status, "not a sessions.exit_status value"),
    };

    public static ExitStatus ParseExitStatus(string text) => text switch
    {
        "normal" => ExitStatus.Normal,
        "crashed" => ExitStatus.Crashed,
        "unhooked_safety" => ExitStatus.UnhookedSafety,
        "degraded" => ExitStatus.Degraded,
        "interrupted" => ExitStatus.Interrupted,
        _ => throw new InvalidDataException($"sessions.exit_status carries '{text}', which this build does not know"),
    };

    private static long? L(DbDataReader r, string column) => SqliteReaders.Int64(r, r.GetOrdinal(column));

    private static int? I(DbDataReader r, string column) => SqliteReaders.Int64(r, r.GetOrdinal(column)) is { } v ? (int)v : null;

    private static double? D(DbDataReader r, string column) => SqliteReaders.Double(r, r.GetOrdinal(column));

    private static string? S(DbDataReader r, string column) => SqliteReaders.String(r, r.GetOrdinal(column));
}
