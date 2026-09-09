namespace FrameLedger.Application.Capture;

/// <summary>
/// The NVIDIA driver's per-process NGX word (<c>NvAPI_NGX_GetNGXOverrideState</c>), read out of
/// process beside each module snapshot. Every way it can fail is an outcome on the state, never a throw.
/// </summary>
public interface INgxDriverProbe
{
    NgxDriverState Run(int pid);
}
