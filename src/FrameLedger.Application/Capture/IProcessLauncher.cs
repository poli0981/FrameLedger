namespace FrameLedger.Application.Capture;

/// <summary>
/// Launch mode's first step (<c>04_CAPTURE</c> §Launch mode): start the consented executable and
/// hold it from birth. Never <c>CREATE_SUSPENDED</c>; never terminates what it started.
/// </summary>
public interface IProcessLauncher
{
    /// <summary>The new pid and its held liveness — the caller owns the liveness — or null when it could not be started or pinned.</summary>
    (int Pid, ITargetLiveness Alive)? Start(string exePath, string arguments);
}
