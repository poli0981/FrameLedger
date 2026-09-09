namespace FrameLedger.Application.Capture;

/// <summary>
/// One out-of-process look at which census-named modules the target has loaded, with the
/// file version of each. Taken beside every guard scan; never throws — an unreadable target
/// is counted on the set, not thrown into the loop.
/// </summary>
public interface IRuntimeModuleSnapshot
{
    RuntimeModuleSet Take(int pid);
}
