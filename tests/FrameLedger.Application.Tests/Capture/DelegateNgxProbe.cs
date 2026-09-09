using FrameLedger.Application.Capture;

namespace FrameLedger.Application.Tests.Capture;

internal sealed class DelegateNgxProbe(Func<int, NgxDriverState> run) : INgxDriverProbe
{
    public NgxDriverState Run(int pid) => run(pid);
}
