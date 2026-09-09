using System.Collections.Concurrent;
using System.Reflection;
using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Native;

/// <summary>
/// The one <c>DllImport</c> resolver this assembly may have — the runtime allows exactly one per assembly, and
/// the second native facade (P2 PR-E2, <c>NativeNvapiBridge</c> after <c>NativeAntiCheatGuard</c>) found that out:
/// two static constructors each calling <c>SetDllImportResolver</c> is a <see cref="InvalidOperationException"/>
/// in whichever runs second. Every facade claims its library name here, with the one policy that differs.
/// </summary>
/// <remarks>
/// <para>
/// <b>Absolute path, never a search.</b> <c>AppContext.BaseDirectory</c> joined with the library name: not the
/// current directory, not the PATH, not the application directory through the loader's own probing. The reason
/// is written on the guard, whose replacement on the probe order would replace the whole anti-cheat gate; the
/// bridge inherits the rule because one facade with a looser rule is the precedent the next one cites.
/// </para>
/// <para>
/// <b>Required or optional</b> is the only policy a facade chooses. The guard is required: absent means
/// <see cref="NativeLibrary.Load(string)"/> throws, and a missing guard never degrades into "carry on without one".
/// The bridge is optional: absent answers <see cref="IntPtr.Zero"/>, the P/Invoke raises
/// <see cref="DllNotFoundException"/> under its System32-only default search, and L3 reports itself unavailable.
/// </para>
/// </remarks>
internal static class BesideThisAssembly
{
    private static readonly ConcurrentDictionary<string, bool> _required = new(StringComparer.Ordinal);

    static BesideThisAssembly()
    {
        NativeLibrary.SetDllImportResolver(typeof(BesideThisAssembly).Assembly, Resolve);
    }

    /// <summary>Claims a library name for this resolver; a facade calls it from its static constructor.</summary>
    /// <param name="libraryName">The file name exactly as the <c>DllImport</c> attributes spell it.</param>
    /// <param name="required">Whether absence is a throw (the guard) or a zero handle (the bridge).</param>
    public static void Claim(string libraryName, bool required)
    {
        ArgumentException.ThrowIfNullOrEmpty(libraryName);
        _required[libraryName] = required;
    }

    /// <summary>The full path a claimed library is loaded from, for the facades' own presence checks.</summary>
    public static string PathOf(string libraryName) => Path.Combine(AppContext.BaseDirectory, libraryName);

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!_required.TryGetValue(libraryName, out bool required))
        {
            return IntPtr.Zero;
        }

        string full = PathOf(libraryName);
        if (required)
        {
            return NativeLibrary.Load(full);
        }

        return NativeLibrary.TryLoad(full, out IntPtr handle) ? handle : IntPtr.Zero;
    }
}
