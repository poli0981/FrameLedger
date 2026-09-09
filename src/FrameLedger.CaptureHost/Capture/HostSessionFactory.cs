using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Application.Recording;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Capture;
using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// The session and its collaborators, wired the only way this host allows; the recorder supplies the observer.
/// Owns the one NGX probe (P2 PR-E2: in-process through the bridge, where <c>fl-probe-nvapi.exe</c> used to be
/// spawned) for as long as the verb runs: one <c>NvAPI_Initialize</c> per host, not per session.
/// </summary>
internal sealed class HostSessionFactory(IGameConsentStore store, int seconds, IProcessLauncher? launcher = null) : ICaptureSessionFactory, IDisposable
{
    private readonly NvapiNgxStateProbe _ngx = new();

    public void Dispose() => _ngx.Dispose();

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
            _ngx,
            launcher ?? new ProcessLauncher(),
            observer);
    }
}
