using System.ComponentModel;
using System.Diagnostics;
using FrameLedger.CaptureHost.Consume;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// One out-of-process look at which census-named modules the target has loaded, and what
/// version each file on disk carries.
/// </summary>
/// <remarks>
/// <para>
/// <b>Consumer-side, deliberately.</b> The writer could read the same thing on its watchdog, but
/// that costs <c>version.dll</c> as an import of the injected DLL, an allocation per module, and
/// a packed word per module in <c>FlWriterState.reserved[]</c> — twenty bytes for up to twenty
/// names does not fit. Out here it is <see cref="Process.Modules"/>, which is the loader's
/// list through documented PSAPI, plus <see cref="FileVersionInfo"/> on the path it names.
/// </para>
/// <para>
/// <b>Never throws.</b> A target that is protected, elevated, or already gone yields an
/// unreadable snapshot with whatever was collected before the failure; the loop records that
/// rather than ending the session over a diagnostic.
/// </para>
/// </remarks>
internal static class RuntimeModuleSnapshot
{
    public static RuntimeModuleSet Take(int pid, IReadOnlyCollection<string> fileNames)
    {
        ArgumentNullException.ThrowIfNull(fileNames);
        var found = new List<RuntimeModuleInfo>();
        try
        {
            using Process target = Process.GetProcessById(pid);
            foreach (ProcessModule module in target.Modules)
            {
                using (module)
                {
                    if (module.ModuleName is not { } name || module.FileName is not { } path)
                    {
                        continue;
                    }

                    if (!fileNames.Contains(name, StringComparer.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    found.Add(Describe(name, path));
                }
            }

            return new RuntimeModuleSet(found, Snapshots: 1, Unreadable: 0);
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or ArgumentException
                                       or NotSupportedException)
        {
            return new RuntimeModuleSet(found, Snapshots: 1, Unreadable: 1);
        }
    }

    /// <summary>
    /// The fixed-part version numbers, not the string: Streamline stamps its string as
    /// <c>2,8,0,0</c>, and a module with no version resource at all — Cyberpunk's
    /// <c>ffx_fsr3_x64.dll</c> — has neither, which is reported as null rather than as 0.0.0.0.
    /// </summary>
    private static RuntimeModuleInfo Describe(string name, string path)
    {
        try
        {
            FileVersionInfo info = FileVersionInfo.GetVersionInfo(path);
            Version? parsed = info.FileVersion is null
                ? null
                : new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart, info.FilePrivatePart);
            return new RuntimeModuleInfo(name, path, info.FileVersion, parsed);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return new RuntimeModuleInfo(name, path, null, null);
        }
    }
}
