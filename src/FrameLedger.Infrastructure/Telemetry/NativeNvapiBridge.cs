using System.Runtime.InteropServices;
using System.Text;
using FrameLedger.Infrastructure.Native;

namespace FrameLedger.Infrastructure.Telemetry;

/// <summary>
/// <c>FrameLedger.NvapiBridge.dll</c> behind <see cref="INvapiBridge"/>: the second P/Invoke facade after
/// <c>NativeAntiCheatGuard</c>, and it follows the same rule — loaded by absolute path from beside this
/// assembly, never from a search path (<c>18_GPU_VENDOR_APIS</c> §L3; P2 PR-E2).
/// </summary>
/// <remarks>
/// <para>
/// <b>Absent is not a fault.</b> A build without the native tree (or a fresh clone before <c>build.ps1
/// native</c>) has no DLL beside the binary; <see cref="Init"/> answers <see cref="Unavailable"/> and L3
/// disables itself the way a machine without an NVIDIA driver does. The guard's absence is a safety hole
/// and throws; the bridge's absence is a missing telemetry layer and reports itself.
/// </para>
/// <para>
/// <b>Never loaded into a game.</b> The Agent loads it into its own process; the name carries none of the
/// words the guard's §S18 heuristic scans a launcher's ancestors for.
/// </para>
/// </remarks>
public sealed class NativeNvapiBridge : INvapiBridge
{
    /// <summary>What <see cref="Init"/> answers when the DLL is not beside this assembly at all.</summary>
    public const int Unavailable = -1999;

    /// <summary><c>FL_NV_NO_GPU</c>: the driver initialised and enumerated no GPU. NGX words are still queryable.</summary>
    public const int NoGpu = -1002;

    private const string _dll = "FrameLedger.NvapiBridge.dll";

    private bool _disposed;

    /// <summary>
    /// References this instance holds on the DLL's refcounted <c>FlNvInit</c>. Two owners in one process
    /// (the L3 source and the NGX probe) each hold their own instance, so one instance's <see cref="Dispose"/>
    /// releases exactly what it took and never the other's.
    /// </summary>
    private int _live;

    static NativeNvapiBridge()
    {
        // Optional where the guard is required: absent is a zero handle, a DllNotFoundException under the
        // System32-only default search, and Unavailable from Init -- never a search of anything else.
        BesideThisAssembly.Claim(_dll, required: false);
    }

    /// <summary>Whether the DLL is beside this assembly, without loading it.</summary>
    public static bool IsPresent => File.Exists(BesideThisAssembly.PathOf(_dll));

    public int Init()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!IsPresent)
        {
            return Unavailable;
        }

        try
        {
            int status = FlNvInit();
            if (status is 0 or NoGpu)
            {
                // Both take a reference on the native side (NGX words need no GPU handle).
                _live++;
            }

            return status;
        }
        catch (DllNotFoundException)
        {
            return Unavailable;
        }
    }

    public int ReadSample(out NvapiSample sample)
    {
        sample = new NvapiSample { Size = (uint)Marshal.SizeOf<NvapiSample>() };
        return FlNvReadSample(ref sample);
    }

    public int NgxState(uint pid, out NvapiNgxWords words)
    {
        words = new NvapiNgxWords { Size = (uint)Marshal.SizeOf<NvapiNgxWords>() };
        return FlNvNgxState(pid, ref words);
    }

    public int DriverVersion(out uint version, out string branch)
    {
        var buffer = new byte[64];
        int status = FlNvDriverVersion(out version, buffer, (uint)buffer.Length);
        int end = Array.IndexOf(buffer, (byte)0);
        branch = Encoding.ASCII.GetString(buffer, 0, end < 0 ? buffer.Length : end);
        return status;
    }

    public void Shutdown()
    {
        if (_live > 0)
        {
            _live--;
            FlNvShutdown();
        }
    }

    /// <summary>The DLL's own build id, for the mirror test; null when it is absent.</summary>
    public static string? BuildId()
    {
        if (!IsPresent)
        {
            return null;
        }

        IntPtr p = FlNvBuildId();
        return p == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(p);
    }

    public static uint SampleSize() => FlNvSampleSize();

    public static uint NgxStateSize() => FlNvNgxStateSize();

    public static uint AbiVersion() => FlNvAbiVersion();

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        while (_live > 0)
        {
            Shutdown();
        }
    }

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlNvInit();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void FlNvShutdown();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlNvReadSample(ref NvapiSample sample);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlNvNgxState(uint pid, ref NvapiNgxWords words);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlNvDriverVersion(out uint version, [Out] byte[] branch, uint capacity);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint FlNvAbiVersion();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint FlNvSampleSize();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint FlNvNgxStateSize();

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr FlNvBuildId();
}
