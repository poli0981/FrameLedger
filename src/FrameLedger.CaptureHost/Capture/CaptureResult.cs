using FrameLedger.CaptureHost.Consume;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>What one session did.</summary>
internal sealed record CaptureResult
{
    public required SessionEndReason Reason { get; init; }

    public AntiCheatVerdict Verdict { get; init; }

    public ShmAttachRefusal AttachRefusal { get; init; }

    public IReadOnlyList<FlFrameRecord> Records { get; init; } = [];

    public FlWriterState WriterState { get; init; }

    public FlShmHandshake Handshake { get; init; }

    /// <summary>
    /// The supervisor's completed-evaluation count, as published to
    /// <c>FlControlBlock.guardTicks</c>. Never a count this loop kept of its own.
    /// </summary>
    public uint GuardTicksPublished { get; init; }

    public long TotalDropped { get; init; }

    public long TotalGaps { get; init; }

    /// <summary>Drain ticks the loop completed — the denominator for <see cref="ForegroundTicks"/>.</summary>
    public long DrainTicks { get; init; }

    /// <summary>
    /// Drain ticks on which the target owned the foreground window.
    /// </summary>
    /// <remarks>
    /// <b>Zero and "fewer than <see cref="DrainTicks"/>" are different findings and the report
    /// must not merge them.</b> Zero means the target owned no top-level window we could ever
    /// see — <c>hook-harness</c> presents to a composition swapchain and has none — so focus
    /// says nothing about that session. A count between the two means the operator switched
    /// away, which stops frame generation and mixes two states into one window: measured
    /// 2026-08-16, that produced an achieved <c>presents / batch</c> of 1.84 against a title
    /// configured for ×2.
    /// </remarks>
    public long ForegroundTicks { get; init; }

    /// <summary>
    /// The census-named modules the target had loaded, with their file versions, merged over every
    /// snapshot the loop took beside a guard scan. <see cref="RuntimeModuleSet.Empty"/> when the loop
    /// had no snapshot source.
    /// </summary>
    public RuntimeModuleSet RuntimeModules { get; init; } = RuntimeModuleSet.Empty;

    /// <summary>
    /// The NVIDIA driver's per-process NGX word, probed out of process beside each module snapshot and merged.
    /// <see cref="NgxDriverState.NotRun"/> when the loop had no probe.
    /// </summary>
    public NgxDriverState NgxDriver { get; init; } = NgxDriverState.NotRun;

    /// <summary>
    /// QPC timestamps of every moment this host touched the target's process — a guard scan
    /// completing, followed by the module snapshot — in the records' own clock, so a stall in the
    /// frame stream can be checked against what FrameLedger itself was doing at that moment.
    /// </summary>
    public IReadOnlyList<long> TouchQpc { get; init; } = [];

    /// <summary>
    /// Launch mode only: from the process being started to the guard's answer — the wait for a
    /// presentation runtime plus the full scan and the injection. Null in attach mode.
    /// </summary>
    public TimeSpan? LaunchWait { get; init; }

    /// <summary>The captured process, for the report's look-ups (the native log is named after it).</summary>
    public int TargetPid { get; init; }
}
