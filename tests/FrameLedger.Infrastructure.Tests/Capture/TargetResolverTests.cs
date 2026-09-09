using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Infrastructure.Capture;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Tests.Capture;

/// <summary>
/// The resolver against real processes sharing one image, which is the shape a Chromium
/// title presents.
/// </summary>
/// <remarks>
/// <para>
/// <c>cmd.exe</c> is copied under a unique name so <c>GetProcessesByName</c> sees only the
/// instances this test started — the developer's own terminals would otherwise be candidates.
/// </para>
/// <para>
/// <b>Positive shapes only, and that is a measured decision.</b> The two refusal cases that
/// used to live here — two untyped instances, two GPU processes — went red on the hosted
/// runner on 2026-09-04, twice, in two different cases, with the resolver seeing ONE of the two
/// processes it had just been shown were enumerable; neither reproduced locally in a dozen
/// runs. A refusal case that passes only when the runner happens to enumerate both children is
/// not pinning the refusal. The refusal logic is pure and is pinned in
/// <c>ChromiumGpuProcessTests</c>; what a real process proves that a fake cannot — the kernel
/// command-line query and the pick, end to end — is what the cases below keep.
/// </para>
/// <para>
/// <b>Copied beside the test binary, NOT into the temp directory.</b> On the hosted runner
/// <c>Path.GetTempPath()</c> comes back in 8.3 form (<c>C:\Users\RUNNER~1\...</c>) while the
/// kernel reports the process image by its long name, so the normalised paths never matched
/// and the resolver honestly answered <c>TargetNotRunning</c> — measured on CI 2026-09-03.
/// </para>
/// </remarks>
public sealed class TargetResolverTests : IDisposable
{
    private readonly string _exe;
    private readonly List<Process> _children = [];

    public TargetResolverTests()
    {
        _exe = Path.Combine(AppContext.BaseDirectory, $"flcmd-{Guid.NewGuid():N}.exe");
        File.Copy(Path.Combine(Environment.SystemDirectory, "cmd.exe"), _exe);
    }

    private Process Start(string args)
    {
        var p = Process.Start(new ProcessStartInfo(_exe, args) { UseShellExecute = false, CreateNoWindow = true })!;
        _children.Add(p);
        return p;
    }

    /// <summary>
    /// A freshly started process is not always in <c>GetProcessesByName</c>'s snapshot yet.
    /// Measured on the hosted runner 2026-09-04: two instances started back to back, the
    /// resolver enumerated one, and "two untyped refuse" resolved to the one it saw. Wait for
    /// the state rather than the clock (HANDOFF §Traps: no budget sized on a machine's rate).
    /// </summary>
    private void WaitUntilAllVisible()
    {
        string name = Path.GetFileNameWithoutExtension(_exe);
        SpinWait.SpinUntil(() =>
        {
            Process[] seen = Process.GetProcessesByName(name);
            int count = 0;
            foreach (Process p in seen)
            {
                try
                {
                    // VISIBLE MEANS READABLE, not merely enumerated. A process can be in the
                    // snapshot before its image is mapped, with MainModule null -- measured on
                    // the hosted runner 2026-09-04 (OneInstanceResolves, twice in an hour): the
                    // count below reached 1 and the resolver then saw a candidate with no image.
                    // The resolver now counts that as unreadable rather than absent, so a test
                    // that resolved at that instant would read TargetAmbiguous -- still not the
                    // state under test. Wait for the state the case is about.
                    if (p.MainModule?.FileName is not null)
                    {
                        count++;
                    }
                }
                catch (Exception ex) when (ex is InvalidOperationException or System.ComponentModel.Win32Exception)
                {
                    // Not readable yet; keep waiting.
                }
                finally
                {
                    p.Dispose();
                }
            }

            return count >= _children.Count;
        }, TimeSpan.FromSeconds(10)).Should().BeTrue("every started instance must be enumerable AND readable before resolving");
    }

    private int? ResolveNow(out SessionEndReason reason)
    {
        WaitUntilAllVisible();
        return new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out reason);
    }

    [Fact]
    public void OneInstanceResolves()
    {
        Process only = Start("/c ping -n 30 127.0.0.1 > nul");

        int? pid = ResolveNow(out SessionEndReason reason);

        reason.Should().Be(SessionEndReason.Running);
        pid.Should().Be(only.Id);
    }

    [Fact]
    public void TheChromiumGpuProcessIsPickedOutOfItsSiblings()
    {
        // The Flower in Us shape: three processes, one image path, one of them the GPU process.
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=renderer");
        Process gpu = Start("/c ping -n 30 127.0.0.1 > nul & rem --type=gpu-process");
        Start("/c ping -n 30 127.0.0.1 > nul & rem browser");

        int? pid = ResolveNow(out SessionEndReason reason);

        reason.Should().Be(SessionEndReason.Running);
        pid.Should().Be(gpu.Id);
    }

    [Fact]
    public void TheNwJsShapeResolvesToTheBrowserProcess()
    {
        // Flower in Us, measured: one untyped process and five typed children, no gpu-process.
        Process browser = Start("/c ping -n 30 127.0.0.1 > nul & rem --nwapp=D:\\g");
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=crashpad-handler");
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=utility");
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=renderer");

        int? pid = ResolveNow(out SessionEndReason reason);

        reason.Should().Be(SessionEndReason.Running);
        pid.Should().Be(browser.Id);
    }

    public void Dispose()
    {
        foreach (Process p in _children)
        {
            try
            {
                p.Kill(entireProcessTree: true);
                // The image stays mapped until the process is actually gone; deleting before
                // that is the UnauthorizedAccessException CI reported on 2026-09-03.
                p.WaitForExit(5000);
            }
            catch (InvalidOperationException)
            {
                // already gone
            }

            p.Dispose();
        }

        try
        {
            File.Delete(_exe);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // still mapped by a dying child; a stray copy beside the test binary is harmless
        }
    }
}
