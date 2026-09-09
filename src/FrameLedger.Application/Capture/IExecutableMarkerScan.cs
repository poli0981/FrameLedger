namespace FrameLedger.Application.Capture;

/// <summary>
/// Reads the executable FILE on disk for vendor SDK strings (<c>HANDOFF</c> 7b) — after a
/// session, so the read never overlaps the launch. Every failure is an
/// <see cref="ExecutableMarkers.Error"/>, never a throw.
/// </summary>
public interface IExecutableMarkerScan
{
    ExecutableMarkers Scan(string exePath);
}
