using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace FrameLedger.Infrastructure.Io;

/// <summary>
/// A process handle that is actually held, so the pid cannot be recycled under us.
/// </summary>
/// <remarks>
/// <para>
/// <b>This exists because <c>System.Diagnostics.Process</c> does not do it, and the
/// difference was measured rather than reasoned about.</b> The capture host's first
/// version used <c>Process.GetProcessById(pid)</c> and read <c>HasExited</c>, on the
/// stated grounds that "a handle opened once keeps the identity pinned". A probe over
/// the live object on .NET 10.0.10 found <c>_haveProcessHandle == false</c> and
/// <c>_processHandle == null</c> both after construction and after reading
/// <c>HasExited</c>: <c>GetProcessById</c> opens nothing, and <c>HasExited</c> opens a
/// transient handle and releases it in its own <c>finally</c>. **No handle was held at
/// any point and the pid was never reserved**, so the mechanism three files and one
/// ledger entry claimed did not exist.
/// </para>
/// <para>
/// That mattered twice. Between resolving a pid and injecting into it there is an
/// awaited file read, so an exit-and-recycle in that window would have injected into a
/// stranger on the strength of a consent record for a different binary — the anti-cheat
/// gate still scans the real pid, so not a rule-2 bypass, but a rule-1 one. And
/// mid-session, <c>HasExited</c> against a recycled pid answers about the NEW process,
/// so the session would never end.
/// </para>
/// <para>
/// <b>The rights are the narrowest that answer the question</b>, and they are named
/// here rather than left to a framework's discretion, which is what
/// <c>05_DETECTION</c> asks for: <c>SYNCHRONIZE</c> to wait on it and
/// <c>PROCESS_QUERY_LIMITED_INFORMATION</c> to read its exit code. No
/// <c>VM_READ</c>, no <c>VM_WRITE</c>, no <c>CREATE_THREAD</c> — nothing this handle
/// can do touches memory, so CLAUDE.md rule 4 is untouched.
/// </para>
/// <para>
/// In <c>Infrastructure</c> because CLAUDE.md §Solution layout puts every P/Invoke on
/// this side of the line.
/// </para>
/// </remarks>
public sealed class HeldProcessHandle : IDisposable
{
    private const uint _synchronize = 0x00100000;
    private const uint _queryLimitedInformation = 0x1000;
    private const uint _waitObject0 = 0x0;
    private const uint _stillActive = 259;

    private readonly SafeProcessHandle _handle;

    private HeldProcessHandle(SafeProcessHandle handle) => _handle = handle;

    /// <summary>Opens and HOLDS a handle to <paramref name="pid"/>, or returns null.</summary>
    /// <remarks>
    /// Null means the process is already gone, or is one we may not open at all — a
    /// protected process, or another user's. <c>19_SAFETY</c> §Elevated / protected
    /// targets: report "cannot attach" and do not escalate creatively.
    /// </remarks>
    public static HeldProcessHandle? TryOpen(int pid)
    {
        SafeProcessHandle? handle = null;
        try
        {
            handle = OpenProcess(_synchronize | _queryLimitedInformation, false, (uint)pid);
            if (handle.IsInvalid)
            {
                return null;
            }

            var held = new HeldProcessHandle(handle);
            handle = null;    // ownership transferred; the finally must not close it
            return held;
        }
        finally
        {
            handle?.Dispose();
        }
    }

    /// <summary>
    /// True once the process has exited. Answers about the process this handle was
    /// opened for, and never about a later occupant of the same pid.
    /// </summary>
    public bool HasExited
    {
        get
        {
            // The wait is the primary signal: a process object is signalled on exit, and asking with a
            // zero timeout is a poll rather than a block. GetExitCodeProcess is the corroboration,
            // because STILL_ACTIVE (259) is also a legal exit code — a process that returned 259 would
            // look alive to the exit code alone, which is the documented trap in that API.
            if (WaitForSingleObject(_handle, 0) == _waitObject0)
            {
                return true;
            }

            return GetExitCodeProcess(_handle, out uint code) && code != _stillActive;
        }
    }

    public void Dispose() => _handle.Dispose();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeProcessHandle OpenProcess(uint desiredAccess,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandle, uint processId);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(SafeProcessHandle handle, uint milliseconds);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetExitCodeProcess(SafeProcessHandle handle, out uint exitCode);
}
