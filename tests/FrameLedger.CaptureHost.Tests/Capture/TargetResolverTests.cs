using System.Diagnostics;
using FluentAssertions;
using FrameLedger.CaptureHost.Capture;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.CaptureHost.Tests.Capture;

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

    [Fact]
    public void OneInstanceResolves()
    {
        Process only = Start("/c ping -n 30 127.0.0.1 > nul");

        int? pid = new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out SessionEndReason reason);

        reason.Should().Be(SessionEndReason.Running);
        pid.Should().Be(only.Id);
    }

    [Fact]
    public void TwoInstancesWithoutTheFlagStillRefuse()
    {
        Start("/c ping -n 30 127.0.0.1 > nul");
        Start("/c ping -n 30 127.0.0.1 > nul & rem second");

        int? pid = new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out SessionEndReason reason);

        pid.Should().BeNull();
        reason.Should().Be(SessionEndReason.TargetAmbiguous, "an ordinary title running twice is still a guess");
    }

    [Fact]
    public void TheChromiumGpuProcessIsPickedOutOfItsSiblings()
    {
        // The Flower in Us shape: three processes, one image path, one of them the GPU process.
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=renderer");
        Process gpu = Start("/c ping -n 30 127.0.0.1 > nul & rem --type=gpu-process");
        Start("/c ping -n 30 127.0.0.1 > nul & rem browser");

        int? pid = new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out SessionEndReason reason);

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

        int? pid = new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out SessionEndReason reason);

        reason.Should().Be(SessionEndReason.Running);
        pid.Should().Be(browser.Id);
    }

    [Fact]
    public void TwoGpuProcessesAreTwoTitlesAndRefuse()
    {
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=gpu-process");
        Start("/c ping -n 30 127.0.0.1 > nul & rem --type=gpu-process");

        int? pid = new TargetResolver().Resolve(ExecutableIdentity.Normalise(_exe), out SessionEndReason reason);

        pid.Should().BeNull();
        reason.Should().Be(SessionEndReason.TargetAmbiguous);
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
