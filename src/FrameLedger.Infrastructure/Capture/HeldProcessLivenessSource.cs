using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Capture;

/// <summary>
/// <see cref="HeldProcessHandle.TryOpen"/> behind <see cref="ITargetLivenessSource"/>: the pin
/// attach mode takes before the injection, held for the whole session (§S29(e)).
/// </summary>
public sealed class HeldProcessLivenessSource : ITargetLivenessSource
{
    public ITargetLiveness? TryPin(int pid)
    {
        // The liveness OWNS the handle from here; nulling the local is how the transfer is stated.
        HeldProcessHandle? handle = null;
        try
        {
            handle = HeldProcessHandle.TryOpen(pid);
            if (handle is null)
            {
                return null;
            }

            var alive = new ProcessTargetLiveness(handle, pid);
            handle = null;
            return alive;
        }
        finally
        {
            handle?.Dispose();
        }
    }
}
