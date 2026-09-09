using System.ComponentModel;
using System.Diagnostics;
using FrameLedger.Application.Capture;

namespace FrameLedger.CaptureHost.Capture;

/// <summary>
/// Runs <c>fl-probe-nvapi --ngx-state &lt;pid&gt;</c> beside this host and parses its machine line.
/// </summary>
/// <remarks>
/// <para>
/// <b>Out of process, on purpose.</b> The probe links <c>nvapi64.lib</c> (MIT, vendored) and asks the driver about
/// a pid; nothing is injected, nothing in the target is touched, no consent is involved. Spawning the tool this
/// repo already builds keeps the native layer reachable only through what is already there (CLAUDE.md: no
/// P/Invoke outside <c>Infrastructure</c>) — the day the shipped Agent wants this fact, it goes through
/// <c>Infrastructure</c> as a native call, not through a child process.
/// </para>
/// <para>
/// The probe is staged beside the host by the project file the way the Overlay payload is; a missing binary is
/// an outcome the report prints, never an exception the loop sees.
/// </para>
/// </remarks>
internal sealed class NgxDriverProbe : INgxDriverProbe
{
    private const string _probeFileName = "fl-probe-nvapi.exe";

    private static readonly TimeSpan _budget = TimeSpan.FromSeconds(5);

    public static string ProbePath => Path.Combine(AppContext.BaseDirectory, _probeFileName);

    /// <inheritdoc />
    NgxDriverState INgxDriverProbe.Run(int pid) => Run(pid);

    public static NgxDriverState Run(int pid) => Run(pid, ProbePath);

    public static NgxDriverState Run(int pid, string probePath)
    {
        if (!File.Exists(probePath))
        {
            return NgxDriverState.Of(NgxProbeOutcome.ProbeMissing, probePath);
        }

        try
        {
            using var probe = new Process();
            probe.StartInfo = new ProcessStartInfo(probePath)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
            };
            probe.StartInfo.ArgumentList.Add("--ngx-state");
            probe.StartInfo.ArgumentList.Add(pid.ToString(System.Globalization.CultureInfo.InvariantCulture));
            probe.Start();
            string output = probe.StandardOutput.ReadToEnd();
            if (!probe.WaitForExit((int)_budget.TotalMilliseconds))
            {
                probe.Kill();
                return NgxDriverState.Of(NgxProbeOutcome.ProbeFailed, "the probe did not exit within " + _budget.TotalSeconds + " s");
            }

            return NgxDriverState.Parse(output);
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException or IOException)
        {
            return NgxDriverState.Of(NgxProbeOutcome.ProbeFailed, e.Message);
        }
    }
}
