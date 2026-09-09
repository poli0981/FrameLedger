using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Capture;

/// <summary>
/// <c>ShmRingReader.TryAttach</c> behind <see cref="IRingAttacher"/>, with the build id the
/// handshake is compared against fixed at construction — <c>FlGuardBuildId</c>'s, never the
/// Overlay's own (<c>04_CAPTURE</c> §Ring draining).
/// </summary>
public sealed class ShmRingAttacher : IRingAttacher
{
    private readonly string _ownBuildId;

    public ShmRingAttacher(string ownBuildId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(ownBuildId);
        _ownBuildId = ownBuildId;
    }

    public (ICaptureSink? Sink, ShmAttachRefusal Refusal) TryAttach(int pid)
    {
        // The sink OWNS the reader from here; nulling the local is how the transfer is stated.
        ShmRingReader? reader = null;
        try
        {
            reader = ShmRingReader.TryAttach(pid, _ownBuildId, out ShmAttachRefusal refusal);
            if (reader is null)
            {
                return (null, refusal);
            }

            var sink = new ShmCaptureSink(reader);
            reader = null;
            return (sink, refusal);
        }
        finally
        {
            reader?.Dispose();
        }
    }
}
