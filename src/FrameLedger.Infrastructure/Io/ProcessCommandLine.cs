using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace FrameLedger.Infrastructure.Io;

/// <summary>
/// The command line another process was started with, as the kernel reports it.
/// </summary>
/// <remarks>
/// <para>
/// <b>Served by the kernel, not read out of the target.</b> <c>NtQueryInformationProcess</c>
/// with <c>ProcessCommandLineInformation</c> (class 60, Windows 8.1+) copies the string into
/// a buffer we own; nothing here calls <c>ReadProcessMemory</c>, walks a PEB, or touches the
/// target's address space. That is what keeps this on the right side of CLAUDE.md rule 4 —
/// the same line <c>HeldProcessHandle</c> draws: <c>PROCESS_QUERY_LIMITED_INFORMATION</c>,
/// no <c>VM_READ</c>. (The WMI route, <c>Win32_Process.CommandLine</c>, answers the same
/// question through the same class; this is the direct form of it.)
/// </para>
/// <para>
/// <b>Why it exists.</b> Chromium-based titles — NW.js, Electron, RPG Maker MV/MZ — run
/// several processes from one image path, and only one of them, the GPU process, owns the
/// swapchain. Nothing about a process but its command line says which (<c>--type=gpu-process</c>
/// is Chromium's own flag), so the resolver's ambiguity refusal had no discriminator it was
/// allowed to use. Measured 2026-09-03 on <i>Flower in Us</i>: three <c>Game.exe</c>, one
/// path, <c>TargetAmbiguous</c>.
/// </para>
/// <para>
/// Null means "could not read", never "empty": a process we may not open, one that exited,
/// or a query the OS refused. A caller must treat null as <i>unknown</i> and must not let it
/// narrow a candidate set (<c>TargetResolver</c>'s rule).
/// </para>
/// </remarks>
public static class ProcessCommandLine
{
    private const uint _queryLimitedInformation = 0x1000;
    private const int _processCommandLineInformation = 60;
    private const int _statusInfoLengthMismatch = unchecked((int)0xC0000004);
    private const uint _maxBytes = 1u << 20;

    /// <summary>The command line of <paramref name="pid"/>, or null when it could not be read.</summary>
    public static string? TryRead(int pid)
    {
        if (pid <= 0)
        {
            return null;
        }

        using SafeProcessHandle handle = OpenProcess(_queryLimitedInformation, false, (uint)pid);
        if (handle.IsInvalid)
        {
            return null;
        }

        // Two calls by design: the first asks the kernel how large the answer is, the second
        // receives it. A fixed buffer would either truncate a long command line silently or
        // over-allocate for every call.
        int status = NtQueryInformationProcess(handle, _processCommandLineInformation, nint.Zero, 0, out uint needed);
        if (status != _statusInfoLengthMismatch || needed == 0 || needed > _maxBytes)
        {
            return null;
        }

        nint buffer = Marshal.AllocHGlobal((int)needed);
        try
        {
            status = NtQueryInformationProcess(handle, _processCommandLineInformation, buffer, needed, out _);
            if (status != 0)
            {
                return null;
            }

            // The buffer starts with a UNICODE_STRING { ushort Length; ushort MaximumLength; PWSTR Buffer; }
            // whose Buffer points INTO the same allocation, so nothing outlives the try.
            ushort lengthBytes = (ushort)Marshal.ReadInt16(buffer, 0);
            nint chars = Marshal.ReadIntPtr(buffer, nint.Size);
            if (chars == nint.Zero || lengthBytes == 0)
            {
                return string.Empty;
            }

            return Marshal.PtrToStringUni(chars, lengthBytes / 2);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeProcessHandle OpenProcess(uint desiredAccess,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandle, uint processId);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport("ntdll.dll")]
    private static extern int NtQueryInformationProcess(SafeProcessHandle process, int informationClass,
        nint information, uint length, out uint returnLength);
}
