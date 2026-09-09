using FrameLedger.Application.Capture;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Capture;

internal sealed class DelegateRingAttacher(Func<int, (ICaptureSink? Sink, ShmAttachRefusal Refusal)> attach) : IRingAttacher
{
    public (ICaptureSink? Sink, ShmAttachRefusal Refusal) TryAttach(int pid) => attach(pid);
}
