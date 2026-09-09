using FrameLedger.Application.Capture;

namespace FrameLedger.Application.Tests.Capture;

internal sealed class DelegateProcessLauncher(Func<string, string, (int Pid, ITargetLiveness Alive)?> start) : IProcessLauncher
{
    public (int Pid, ITargetLiveness Alive)? Start(string exePath, string arguments) => start(exePath, arguments);
}
