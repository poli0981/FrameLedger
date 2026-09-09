using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Capture;

/// <summary>
/// A process handle, opened before the injection and held for the whole session.
/// </summary>
/// <remarks>
/// <para>
/// <b>Held, and this class's first version did not actually hold anything.</b> It used
/// <c>Process.GetProcessById(pid)</c> and read <c>HasExited</c>, with a comment saying
/// the kernel would not reuse the pid while a handle was open — which is true of a
/// handle and false of that API. Measured on .NET 10.0.10: <c>GetProcessById</c> opens
/// no handle, and <c>HasExited</c> opens a transient one and releases it in its own
/// <c>finally</c>, so the pid was never reserved and §S29(e) was recorded as closed on
/// a mechanism that did not exist. <see cref="HeldProcessHandle"/> carries the
/// measurement and the rights.
/// </para>
/// <para>
/// <b>A pid we cannot open is a refusal, not a session.</b> <c>TryOpen</c> returning
/// null means the process is already gone or is one we may not open — a protected
/// process, or another user's — and <c>19_SAFETY</c> §Elevated / protected targets is
/// explicit that the answer there is "cannot attach", never a creative escalation. The
/// loop must not proceed to inject into a pid whose identity it could not pin.
/// </para>
/// </remarks>
public sealed class ProcessTargetLiveness(HeldProcessHandle handle, int pid) : ITargetLiveness
{
    private readonly HeldProcessHandle _handle = handle ?? throw new ArgumentNullException(nameof(handle));

    public bool HasExited => _handle.HasExited;

    public int? ExitCode => _handle.ExitCode;

    /// <summary>
    /// Asked of the pid rather than of the held handle, because window ownership is not a
    /// property this handle's rights can answer.
    /// </summary>
    /// <remarks>
    /// The handle carries <c>SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION</c> and nothing
    /// more, deliberately, and no widening is wanted for a diagnostic: the recycle hazard the
    /// handle exists to close does not apply here, because a stale pid answers false and false
    /// is already the value this cannot distinguish from "no window" — it degrades to the state
    /// the report is required to treat as uninformative.
    /// </remarks>
    public bool IsForeground => ForegroundWindowProbe.IsForeground(pid);

    public void Dispose() => _handle.Dispose();
}
