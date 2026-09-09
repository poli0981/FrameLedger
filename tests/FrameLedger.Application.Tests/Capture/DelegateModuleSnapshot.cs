using FrameLedger.Application.Capture;

namespace FrameLedger.Application.Tests.Capture;

internal sealed class DelegateModuleSnapshot(Func<int, RuntimeModuleSet> take) : IRuntimeModuleSnapshot
{
    public RuntimeModuleSet Take(int pid) => take(pid);
}
