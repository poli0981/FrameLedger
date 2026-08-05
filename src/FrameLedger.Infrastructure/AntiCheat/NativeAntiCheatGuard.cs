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

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int FlGuardRulesFilePath([Out] char[] buffer, int cap);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlGuardCheckRules(byte[] json, int length);

    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [DllImport(_guardDll, CallingConvention = CallingConvention.Cdecl)]
    private static extern int FlGuardBuildId([Out] byte[] buffer, int cap);

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

    /// <summary>
    /// The path the native guard reads its rules from, for asserting that this
    /// assembly's independent resolution agrees with it (§S21).
    /// </summary>
    /// <remarks>
    /// Read-only by construction: the ABI has no setter and accepts no path, so
    /// this widens what can be OBSERVED and not what can be chosen. It exists
    /// because two different Win32 resolutions of "the same directory" is exactly
    /// the shape §S21 was — a seeder that writes where the gate does not read
    /// reports success and leaves the guard refusing every title.
    /// </remarks>
    /// <summary>
    /// Would the native guard accept this rules document? Returns
    /// <c>fl::guard::ParseResult</c>; <c>0</c> is <c>kOk</c>.
    /// </summary>
    /// <remarks>
    /// §S20's seeder needs this because nothing managed can answer it:
    /// <c>DetectionRulesFile</c> reads engines/platforms/capabilities and, by
    /// explicit design, never the <c>anticheat</c> block (§S15 — no second
    /// matcher). Validating a candidate with the managed reader would check
    /// everything except the half the hard gate consumes.
    /// <para>
    /// Observation only: buffer in, enum out, no path parameter, nothing
    /// installed. It calls the same <c>ParseRules</c> the gate parses with, so
    /// the thing that validates and the thing that parses are one.
    /// </para>
    /// </remarks>
    public static int NativeCheckRules(byte[] json)
    {
        ArgumentNullException.ThrowIfNull(json);
        return FlGuardCheckRules(json, json.Length);
    }

    public static string NativeRulesFilePath()
    {
        // 1024 mirrors fl::guard::kMaxRulesPathLen. A short buffer would come
        // back as 0 rather than as a truncated path, which would then compare
        // unequal and read as drift — so give it the room the native side has.
        char[] buffer = new char[1024];
        int written = FlGuardRulesFilePath(buffer, buffer.Length);
        return written <= 0 ? string.Empty : new string(buffer, 0, written);
    }

    /// <summary>
    /// The build id this install was compiled from, for the shm handshake comparison
    /// (<c>07_IPC</c> §Protocol rules: a mismatch is a hard refuse-to-attach).
    /// <para>
    /// This is the value <c>04_CAPTURE</c> calls "our own", and until 2026-08-05 the managed side had
    /// none — <c>FL_BUILD_ID</c> is a CMake definition visible only to native targets, so the
    /// comparison could not run in either direction (<c>20_OPEN_QUESTIONS</c> §S23-1).
    /// </para>
    /// <para>
    /// It comes from the GUARD, not the Overlay. Reaching the Overlay's <c>FlGetBuildId</c> would mean
    /// loading the payload into the Agent, which starts its init thread and creates a ring under the
    /// Agent's own pid. Guard and Overlay are built by one CMake run, so the two ids agree by
    /// construction — and a native test asserts it rather than trusting that sentence.
    /// </para>
    /// <para>Empty when the native side refuses to answer, which must never read as "matches".</para>
    /// </summary>
    public static string BuildId()
    {
        // 32 mirrors FlShmHandshake::buildId. The native side refuses rather than truncating, so a
        // short buffer returns 0 and we surface empty — a truncated id is a DIFFERENT id, and
        // comparing one would report drift that is really this call's fault.
        byte[] buffer = new byte[32];
        int written = FlGuardBuildId(buffer, buffer.Length);
        return written <= 0 ? string.Empty : System.Text.Encoding.ASCII.GetString(buffer, 0, written);
    }

    private static async ValueTask<AntiCheatVerdict> RunAsync(Func<AntiCheatVerdict> work, CancellationToken ct)
    {
        // Cancellation is honoured BEFORE the call, never during it. A guard
        // evaluation that is abandoned half way has no verdict, and "we stopped
        // asking" must not become "it was fine".
        ct.ThrowIfCancellationRequested();
        return await Task.Run(work, ct).ConfigureAwait(false);
    }
}
