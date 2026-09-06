using System.ComponentModel;
using System.Diagnostics;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.CaptureHost.Capture;

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
internal static class ProcessLauncher
{
    /// <summary>The Vulkan loader's enable variable for <c>VK_LAYER_FRAMELEDGER_overlay</c> (<c>17_HOOK_ENGINE</c> §Vulkan).</summary>
    public const string VulkanLayerEnableVariable = "FRAMELEDGER_ENABLE_VK_LAYER";

    /// <summary>Start <paramref name="exePath"/> with <paramref name="arguments"/>; null when it could not be started or pinned.</summary>
    public static (int Pid, ITargetLiveness Alive)? Start(string exePath, string arguments)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(exePath);

        var psi = new ProcessStartInfo(exePath, arguments ?? string.Empty)
        {
            UseShellExecute = false,
            WorkingDirectory = Path.GetDirectoryName(exePath) ?? string.Empty,
        };
        psi.Environment[VulkanLayerEnableVariable] = "1";

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
            HeldProcessHandle? held = HeldProcessHandle.TryOpen(process.Id);
            if (held is null)
            {
                return null;
            }

            ITargetLiveness alive = new ProcessTargetLiveness(held, process.Id);
            return (process.Id, alive);
        }
    }
}
