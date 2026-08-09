namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Whether the process we injected into is still there — §S29(e)'s answer, and it
/// deliberately does not come from the shared memory.
/// </summary>
/// <remarks>
/// <c>ShmRingReader</c> holds the section open, so an exited game leaves
/// <c>writeIndex</c> frozen and <c>status</c> <c>READY</c>: byte-for-byte identical
/// to a loading screen. §S26 made it strictly worse by dropping
/// <c>DXGI_PRESENT_TEST</c>, which removed the accidental heartbeat an occluded
/// title used to emit. The signal has to come from the OS.
/// </remarks>
internal interface ITargetLiveness : IDisposable
{
    bool HasExited { get; }
}
