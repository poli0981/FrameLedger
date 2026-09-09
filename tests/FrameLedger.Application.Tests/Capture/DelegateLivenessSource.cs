using FrameLedger.Application.Capture;

namespace FrameLedger.Application.Tests.Capture;

internal sealed class DelegateLivenessSource(Func<int, ITargetLiveness?> pin) : ITargetLivenessSource
{
    public ITargetLiveness? TryPin(int pid) => pin(pid);
}
