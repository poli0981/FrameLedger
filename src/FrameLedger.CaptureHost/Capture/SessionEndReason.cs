namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Why a capture session stopped, or that it has not.
/// </summary>
/// <remarks>
/// <c>04_CAPTURE</c> §Crash and exit classification names four exit statuses; these
/// are finer, because the host has to tell apart states the mapping cannot. In
/// particular <see cref="SafetyUnhook"/> and <see cref="SupervisionLost"/> are ONE
/// value in the shared memory — <c>StopObserving</c> stores
/// <c>FL_STATUS_UNHOOKED</c> for both — so only the side that caused one knows
/// which it was, and <c>legal/DISCLAIMER.md</c> §2 discloses them differently
/// ("the guard fired" against "contact was lost").
/// </remarks>
internal enum SessionEndReason
{
    /// <summary>Nothing has ended. The zero value, so a forgotten assignment does not end a session.</summary>
    Running = 0,

    /// <summary>The target process exited. The ONLY signal that means the session is over.</summary>
    TargetExited,

    /// <summary>Our own guard scan refused and we published the stop.</summary>
    SafetyUnhook,

    /// <summary>
    /// The capture side stopped and we did not ask it to, so it stopped counting
    /// down <c>FL_GUARD_TICK_DEADLINE_MS</c> without our ticks.
    /// </summary>
    SupervisionLost,

    /// <summary>Three hook faults; the Overlay went dormant on its own.</summary>
    WriterSelfDisabled,

    /// <summary>
    /// The Overlay's <c>LoadLibrary</c> detour saw a module the compiled anti-cheat floor
    /// names arrive mid-session and stopped the Overlay from inside, before this host's
    /// 30 s scan could (<c>19_SAFETY</c> §During a session, the in-process half; §S6).
    /// <c>FlWriterState.EarlyStopFamily</c> names which family.
    /// </summary>
    WriterStoppedBlocklisted,

    /// <summary>
    /// The handshake was published and <c>status</c> never left <c>INIT</c>, which
    /// means MinHook failed: the ring will never move.
    /// </summary>
    WriterNeverInstalledHooks,

    RefusedHookNotEnabled,
    RefusedConsentMissing,
    RefusedPreviouslyBlocked,
    RefusedByGuard,

    /// <summary>
    /// The static pre-scan reached no answer. Neither a hit nor a pass, so it is
    /// neither a block nor something to clear (<c>05_DETECTION</c>).
    /// </summary>
    PreScanCouldNotVerify,

    /// <summary>Nothing on this machine is running that executable.</summary>
    TargetNotRunning,

    /// <summary>
    /// The process is there and we could not open a handle to it, so its identity
    /// cannot be pinned for the session.
    /// </summary>
    /// <remarks>
    /// A protected process, or another user's. <c>19_SAFETY</c> §Elevated / protected
    /// targets: report "cannot attach" and record the session as Tier-2 with the reason;
    /// never escalate creatively. (Tier 2 measures nothing — it is the honest record
    /// of a capture that did not happen, not a lesser measurement.)
    /// Proceeding would mean injecting into a pid that could be recycled out from
    /// under the consent record between here and <c>FlGuardedInject</c>.
    /// </remarks>
    TargetCannotBePinned,

    /// <summary>
    /// The executable on disk could not be read, so the consented fingerprint cannot
    /// be compared against anything.
    /// </summary>
    /// <remarks>
    /// "Could not look" refuses. The alternative — substituting the stored fingerprint
    /// for the observed one — turns the comparison into a self-comparison that always
    /// passes, which is the one polarity everything else here is built to avoid
    /// (<c>ConsentProvenance.NotRecorded = 0</c>, <c>AntiCheatVerdict</c>'s default,
    /// <c>ShmAttachRefusal.NotEvaluated</c>).
    /// </remarks>
    ExecutableUnreadable,

    /// <summary>
    /// A guard evaluation threw mid-session. The tick did not advance — which is the
    /// correct outcome — but the records drained so far are still returned.
    /// </summary>
    /// <remarks>
    /// The two consequences are separable and were conflated: not advancing the tick
    /// is what <c>GuardSupervisor</c> requires, while losing the session's data with
    /// the stack is not. The capture side still stops, because a tick that does not
    /// advance is what <c>FL_GUARD_TICK_DEADLINE_MS</c> counts.
    /// </remarks>
    SupervisionFaulted,

    /// <summary>
    /// More than one process is running it. Refusing is the only safe answer:
    /// picking one would be a guess about which the consent record was for.
    /// </summary>
    TargetAmbiguous,

    /// <summary>The ring could not be attached to, and the refusal was not retryable.</summary>
    AttachRefused,

    /// <summary>Launch mode: the executable could not be started, or the new process could not be pinned.</summary>
    LaunchCannotStart,

    /// <summary>
    /// Launch mode: the guard was waiting for a presentation runtime and the target exited first
    /// (<see cref="Domain.AntiCheat.AntiCheatRefusalReason.LaunchTargetExited"/>). Nothing was injected.
    /// </summary>
    LaunchTargetExited,

    /// <summary>
    /// Launch mode: the budget ran out with no presentation runtime mapped
    /// (<see cref="Domain.AntiCheat.AntiCheatRefusalReason.LaunchNoPresentationRuntime"/>). Nothing was injected.
    /// </summary>
    LaunchNoPresentationRuntime,
}
