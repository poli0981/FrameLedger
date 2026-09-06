using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Decides whether a session has ended, from the two things that can say so.
/// </summary>
/// <remarks>
/// <para>
/// <b>§S29(e): the reader cannot tell a dead target from a quiet one.</b>
/// <c>ShmRingReader</c> holds the section open, so a game that exits leaves
/// <c>writeIndex</c> frozen and <c>status</c> <c>READY</c> — byte-for-byte
/// identical to a loading screen, an alt-tabbed window or a paused session.
/// §S26 made it strictly worse: the writer now DROPS <c>DXGI_PRESENT_TEST</c>, so
/// an occluded title that used to emit a steady stream of probes emits nothing at
/// all. And <c>DllMain</c> deliberately has no <c>DLL_PROCESS_DETACH</c> teardown,
/// so nothing is written on the way out.
/// </para>
/// <para>
/// So the liveness signal comes from the OS, not from the mapping, and this class
/// exists to make that structural: <b>a frozen <c>writeIndex</c> never ends a
/// session, however long it has been frozen.</b> There is no elapsed-time
/// parameter here, and adding one would recreate the defect.
/// </para>
/// </remarks>
internal static class SessionEndClassifier
{
    /// <summary>
    /// <paramref name="targetExited"/> comes from a held process handle;
    /// <paramref name="writerStatus"/> from <c>FlWriterState.Status</c>.
    /// </summary>
    /// <param name="targetExited">Whether the process we injected into has exited.</param>
    /// <param name="writerStatus">The Overlay's own published status.</param>
    /// <param name="weLatchedTheUnhook">
    /// Whether OUR <c>GuardSupervisor</c> refused and we published
    /// <c>unhookRequested</c>. The mapping cannot answer this: <c>StopObserving</c>
    /// stores <c>FL_STATUS_UNHOOKED</c> for the safety stop and for supervision loss
    /// alike, so the two are distinguishable only by the side that caused one.
    /// </param>
    /// <param name="attachSettled">
    /// Whether the attach budget has elapsed. Until it has, <c>INIT</c> means "still
    /// starting"; after it, it means MinHook failed and the ring will never move.
    /// </param>
    public static SessionEndReason Classify(bool targetExited, uint writerStatus, bool weLatchedTheUnhook,
        bool attachSettled)
    {
        // FIRST, because it is the only one that means the session is genuinely over. Every state below
        // is about the capture side, and a capture side that stopped inside a game still running is a
        // different event from a game that closed.
        if (targetExited)
        {
            return SessionEndReason.TargetExited;
        }

        return (FlStatus)writerStatus switch
        {
            FlStatus.Unhooked => weLatchedTheUnhook ? SessionEndReason.SafetyUnhook : SessionEndReason.SupervisionLost,
            FlStatus.SelfDisabled => SessionEndReason.WriterSelfDisabled,
            FlStatus.StoppedBlocklisted => SessionEndReason.WriterStoppedBlocklisted,
            FlStatus.Init when attachSettled => SessionEndReason.WriterNeverInstalledHooks,
            _ => SessionEndReason.Running,
        };
    }
}
