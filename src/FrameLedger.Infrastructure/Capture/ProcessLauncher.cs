using System.ComponentModel;
using System.Diagnostics;
using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Io;
using FrameLedger.Infrastructure.Vulkan;

namespace FrameLedger.Infrastructure.Capture;

/// <summary>
/// Launch mode's first step: start the consented executable and hold it from birth.
/// </summary>
/// <remarks>
/// <para>
/// <b>Not <c>CREATE_SUSPENDED</c>, and the spec's sentence is why.</b> <c>04_CAPTURE</c> §Launch mode
/// wrote <i>create suspended → guard → inject → resume</i>, and <c>20_OPEN_QUESTIONS</c> §S1 measured
/// that a suspended target has loaded nothing: <c>EnumProcessModulesEx</c> fails against it, so the guard
/// cannot run before the loader has. The built shape is <i>create → guard WAITS for a presentation
/// runtime → inject</i> (<c>FlGuardedInjectWhenReady</c>), and nothing in it needs the process held at
/// its first instruction. What the launch buys instead: the pid is ours before any code runs, the handle
/// is held from the start so it cannot recycle under us (§S29(e)), and the Vulkan layer's
/// <c>enable_environment</c> is set — which only the launching process can do (<c>17_HOOK_ENGINE</c>
/// §Vulkan). The layer still gates on its own enabled-list; the variable alone enables nothing.
/// </para>
/// <para>
/// <b>This host never terminates what it launched.</b> A refusal after the launch — no consent, an
/// anti-cheat hit, no runtime inside the budget — leaves the title running unhooked, exactly as the
/// product's Tier 2 does (duration and the reason, no measurement). The operator asked for the game to
/// start; the guard decides only whether FrameLedger goes into it.
/// </para>
/// </remarks>
public sealed class ProcessLauncher : IProcessLauncher
{
    private readonly IReadOnlyDictionary<string, string>? _environment;

    /// <summary>A launcher whose children get <paramref name="environment"/> on top of this process's.</summary>
    /// <param name="environment">
    /// Variables added to every child's environment — the Vulkan layer's, from
    /// <see cref="VkLayerLaunchEnvironment.Variables"/>, when the layer is staged beside the host.
    /// </param>
    public ProcessLauncher(IReadOnlyDictionary<string, string>? environment = null) => _environment = environment;

    /// <inheritdoc />
    public (int Pid, ITargetLiveness Alive)? Start(string exePath, string arguments) => Start(exePath, arguments, _environment);

    /// <summary>Start <paramref name="exePath"/> with <paramref name="arguments"/>; null when it could not be started or pinned.</summary>
    /// <param name="exePath">The consented executable.</param>
    /// <param name="arguments">Its own command line, verbatim.</param>
    /// <param name="environment">
    /// Variables added to the child's environment — the Vulkan layer's, from
    /// <c>VkLayerLaunchEnvironment</c>, when the layer is staged beside this host (P1 item 3).
    /// </param>
    public static (int Pid, ITargetLiveness Alive)? Start(string exePath, string arguments,
        IReadOnlyDictionary<string, string>? environment = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exePath);

        var psi = new ProcessStartInfo(exePath, arguments ?? string.Empty)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(exePath) ?? string.Empty,
        };
        psi.Environment[VkLayerLaunchEnvironment.EnableVariable] = "1";
        foreach ((string name, string value) in environment ?? new Dictionary<string, string>(StringComparer.Ordinal))
        {
            psi.Environment[name] = value;
        }

        Process? process;
        try
        {
            process = Process.Start(psi);
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException or IOException)
        {
            return null;
        }

        if (process is null)
        {
            return null;
        }

        using (process)
        {
            // The same pin attach mode takes, taken here before anything else looks at the pid. The
            // liveness OWNS the handle from here; the caller disposes it with the session (CA2000 is
            // satisfied by that transfer, stated rather than suppressed).
            HeldProcessHandle? held = null;
            try
            {
                held = HeldProcessHandle.TryOpen(process.Id);
                if (held is null)
                {
                    return null;
                }

                ITargetLiveness alive = new ProcessTargetLiveness(held, process.Id);
                held = null;
                return (process.Id, alive);
            }
            finally
            {
                held?.Dispose();
            }
        }
    }
}
