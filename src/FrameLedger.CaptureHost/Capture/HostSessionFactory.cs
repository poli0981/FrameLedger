using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Recording;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Capture;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>The session and its collaborators, wired the only way this host allows; the recorder supplies the observer.</summary>
internal sealed class HostSessionFactory(IGameConsentStore store, int seconds, IProcessLauncher? launcher = null) : ICaptureSessionFactory
{
    public CaptureSession Create(ICaptureObserver observer)
    {
        var guard = new NativeAntiCheatGuard();
        return new CaptureSession(
            store,
            new HookedCaptureGate(guard),
            guard,
            new TargetResolver(HostConsole.Line),
            // Null means the pid could not be PINNED — already gone, protected, or another user's.
            // The loop refuses rather than proceeding to inject into an identity it cannot hold.
            new HeldProcessLivenessSource(),
            new ShmRingAttacher(NativeAntiCheatGuard.BuildId()),
            new CaptureOptions
            {
                // Zero keeps the product behaviour: run until the target exits. A positive
                // --seconds is an operator taking a bounded measurement, and CaptureSession
                // already honoured MaxDuration -- nothing could set it.
                MaxDuration = seconds > 0 ? TimeSpan.FromSeconds(seconds) : TimeSpan.Zero,
            },
            new RuntimeModuleSnapshot(CensusNames.ModuleFileNames),
            new NgxDriverProbe(),
            launcher ?? new ProcessLauncher(),
            observer);
    }
}
