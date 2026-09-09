using System.Globalization;
using FrameLedger.Application.Capture;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// <c>NvAPI_NGX_GetNGXOverrideState</c> for one pid, in-process through the bridge (P2 PR-E2) — what the
/// capture host used to spawn <c>fl-probe-nvapi.exe</c> for. The words are the same; the outcomes map to
/// <see cref="NgxProbeOutcome"/> as the probe's machine line did, so <see cref="NgxDriverState"/> and every
/// consumer of it are untouched.
/// </summary>
/// <remarks>
/// Out of process, by pid, from the driver's own bookkeeping: no game memory, no hook, nothing loaded
/// into the target. Owns one bridge reference for its lifetime; a machine without an NVIDIA driver
/// answers <see cref="NgxProbeOutcome.Degraded"/> on every call and never throws.
/// </remarks>
public sealed class NvapiNgxStateProbe : INgxDriverProbe, IDisposable
{
    private readonly INvapiBridge _bridge;
    private readonly Lock _lock = new();
    private bool _started;
    private bool _usable;
    private string? _refusal;
    private bool _disposed;

    public NvapiNgxStateProbe(INvapiBridge bridge)
    {
        _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
    }

    /// <summary>Over the real DLL: the bridge is born owned here, so there is no hand-off to reason about.</summary>
    public NvapiNgxStateProbe()
    {
        _bridge = new NativeNvapiBridge();
    }

    public NgxDriverState Run(int pid)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        lock (_lock)
        {
            if (!_started)
            {
                _started = true;
                int init = _bridge.Init();
                // NGX words are per driver, not per GPU handle: a driver that enumerated no GPU still answers.
                _usable = init is 0 or NativeNvapiBridge.NoGpu;
                _refusal = init == NativeNvapiBridge.Unavailable
                    ? "the bridge DLL is not beside this binary"
                    : "NvAPI_Initialize " + init.ToString(CultureInfo.InvariantCulture);
            }

            if (!_usable)
            {
                return NgxDriverState.Of(NgxProbeOutcome.Degraded, _refusal);
            }

            int status = _bridge.NgxState(checked((uint)pid), out NvapiNgxWords w);
            if (status != 0)
            {
                return NgxDriverState.Of(NgxProbeOutcome.ProbeFailed, "FlNvNgxState " + status.ToString(CultureInfo.InvariantCulture));
            }

            return w.Status switch
            {
                NvapiNgxWords.Answered => new NgxDriverState(NgxProbeOutcome.Answered, w.Sr, w.Rr, w.Fg, w.ScalingRatio,
                    w.PerformanceMode, w.RenderPreset, w.FrameGenerationCount, w.FrameGenerationPreset, w.FrameGenerationMode,
                    w.Driver, 1, 1, false, null),
                NvapiNgxWords.Unanswered => NgxDriverState.Of(NgxProbeOutcome.Unanswered, "NvAPI " + w.NvapiStatus.ToString(CultureInfo.InvariantCulture)),
                _ => NgxDriverState.Of(NgxProbeOutcome.Degraded, "NvAPI_Initialize " + w.NvapiStatus.ToString(CultureInfo.InvariantCulture)),
            };
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        lock (_lock)
        {
            if (_usable)
            {
                _bridge.Shutdown();
                _usable = false;
            }
        }

        _bridge.Dispose();
    }
}
