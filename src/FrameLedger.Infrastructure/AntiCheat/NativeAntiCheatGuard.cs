using System.Runtime.InteropServices;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Infrastructure.AntiCheat;

/// <summary>
/// The one managed path to the anti-cheat guard: a thin P/Invoke facade over
/// <c>FrameLedger.Guard.dll</c>.
/// </summary>
/// <remarks>
/// <para>
/// <c>20_OPEN_QUESTIONS</c> §S15 item 1. There is deliberately no rules
/// parsing, no blocklist, and no matching here — the native guard owns all of
/// it. Anything this class "decided" would be a second matcher that can
/// disagree with the first, and the day they diverge one of them is wrong with
/// nothing to say which.
/// </para>
/// <para>
/// The native calls are synchronous and can take tens of milliseconds (a
/// process-tree walk plus several enumerations), so they are moved off the
/// caller's thread. They are never called from the UI thread.
/// </para>
/// </remarks>
public sealed class NativeAntiCheatGuard : IAntiCheatGuard
{
    /// <summary>
    /// Mirrors <c>FlGuardResult</c>. Fixed-size buffers, because the native
    /// side allocates nothing — the guard runs on paths where a failed
    /// allocation would have to become a verdict.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private struct FlGuardResult
    {
        public int Reason;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Family;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string Signal;
    }

    private const string _guardDll = "FrameLedger.Guard.dll";

    // WHY THIS IS LOADED BY ABSOLUTE PATH.
    //
    // A FrameLedger.Guard.dll planted earlier on the probe order would REPLACE
    // THE ENTIRE ANTI-CHEAT GATE with whatever the attacker wants it to say.
    // That is a worse outcome than any other DLL-hijack in this application, so
    // the search path is not merely restricted — it is never consulted. The
    // resolver below loads exactly one file, beside this assembly, by full path.
    //
    // The DefaultDllImportSearchPaths attributes are the belt to that braces:
    // System32 is the most restrictive value the analyzers accept, and if the
    // resolver were ever removed the fallback would still not probe the
    // application directory. CA5393 rejects ApplicationDirectory for exactly
    // the reason this comment exists.
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void FlGuardEvaluate(uint targetPid, out FlGuardResult result);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern void FlGuardedInject(uint targetPid, string dllPath, out FlGuardResult result);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern void FlStaticPreScan(string gameDirectory, out FlGuardResult result);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr FlGuardReasonName(int reason);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlGuardReasonCount();

    static NativeAntiCheatGuard()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeAntiCheatGuard).Assembly, Resolve);
    }

    private static IntPtr Resolve(string libraryName, System.Reflection.Assembly assembly, DllImportSearchPath? path)
    {
        if (!string.Equals(libraryName, _guardDll, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        // AppContext.BaseDirectory, not the current directory and not the PATH.
        // If it is not there, FAIL — falling back to a search would reintroduce
        // exactly the hijack this exists to prevent, and a missing guard must
        // never degrade into "carry on without one".
        string full = Path.Combine(AppContext.BaseDirectory, _guardDll);
        return NativeLibrary.Load(full);
    }

    /// <inheritdoc />
    public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default) =>
        RunAsync(() =>
        {
            FlGuardEvaluate(checked((uint)targetPid), out FlGuardResult r);
            return AntiCheatVerdict.FromNative(r.Reason, r.Family, r.Signal);
        }, ct);

    /// <inheritdoc />
    public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
        CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(payloadPath);
        return RunAsync(() =>
        {
            FlGuardedInject(checked((uint)targetPid), payloadPath, out FlGuardResult r);
            return AntiCheatVerdict.FromNative(r.Reason, r.Family, r.Signal);
        }, ct);
    }

    /// <inheritdoc />
    public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
        CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameDirectory);
        return RunAsync(() =>
        {
            FlStaticPreScan(gameDirectory, out FlGuardResult r);
            return AntiCheatVerdict.FromNative(r.Reason, r.Family, r.Signal);
        }, ct);
    }

    /// <summary>
    /// The native name for a reason code. Used by the mirror test to prove the
    /// two enumerations have not drifted; not for user-facing text, which comes
    /// from <c>.resx</c> (<c>09_I18N</c> reviews safety strings as legal text).
    /// </summary>
    public static string NativeReasonName(int reason) =>
        Marshal.PtrToStringAnsi(FlGuardReasonName(reason)) ?? string.Empty;

    /// <summary>How many reason codes the native side declares.</summary>
    public static int NativeReasonCount() => FlGuardReasonCount();

    private static async ValueTask<AntiCheatVerdict> RunAsync(Func<AntiCheatVerdict> work, CancellationToken ct)
    {
        // Cancellation is honoured BEFORE the call, never during it. A guard
        // evaluation that is abandoned half way has no verdict, and "we stopped
        // asking" must not become "it was fine".
        ct.ThrowIfCancellationRequested();
        return await Task.Run(work, ct).ConfigureAwait(false);
    }
}
