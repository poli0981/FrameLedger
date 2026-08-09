using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// That the handle is actually HELD, which is the property the first version claimed
/// and did not have.
/// </summary>
/// <remarks>
/// <c>Process.GetProcessById</c> opens no handle and <c>HasExited</c> opens a transient
/// one and releases it, so the pid was never reserved — measured on .NET 10.0.10, and
/// §S29(e) had been recorded as closed on that mechanism. These assert the property
/// rather than the outcome: an implementation that opens a handle per call would give
/// the same <c>HasExited</c> answers and fail
/// <see cref="TheHandleOutlivesTheProcessSoThePidCannotBeRecycled"/>.
/// </remarks>
public sealed class HeldProcessHandleTests
{
    private static Process StartSleeper() =>
        Process.Start(new ProcessStartInfo("cmd.exe", "/c ping -n 30 127.0.0.1 > nul")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
        })!;

    [Fact]
    public void ALiveProcessHasNotExited()
    {
        // GREEN FIRST: a handle that reported "exited" for everything would satisfy the case below on
        // its own.
        Process p = StartSleeper();
        try
        {
            using HeldProcessHandle? held = HeldProcessHandle.TryOpen(p.Id);

            held.Should().NotBeNull();
            held!.HasExited.Should().BeFalse();
        }
        finally
        {
            p.Kill(entireProcessTree: true);
            p.Dispose();
        }
    }

    [Fact]
    public async Task TheHandleOutlivesTheProcessSoThePidCannotBeRecycled()
    {
        // THE ACTUAL PROPERTY. While a handle is open the kernel keeps the process object — and its pid
        // — reserved, so the id cannot be reissued to a stranger. The observable consequence is that we
        // can still answer about a process that has fully exited, at a pid `Process.GetProcessById` now
        // refuses to resolve at all. An implementation holding nothing cannot do this.
        Process p = StartSleeper();
        int pid = p.Id;
        using HeldProcessHandle? held = HeldProcessHandle.TryOpen(pid);
        held.Should().NotBeNull();

        p.Kill(entireProcessTree: true);
        await p.WaitForExitAsync(TestContext.Current.CancellationToken);
        p.Dispose();

        for (int i = 0; i < 50 && !held!.HasExited; i++)
        {
            await Task.Delay(20, TestContext.Current.CancellationToken);
        }

        held!.HasExited.Should().BeTrue();

        Action resolve = () => Process.GetProcessById(pid).Dispose();
        resolve.Should().Throw<ArgumentException>(
            "the process is gone, so nothing but a held handle can still answer about that pid");
    }

    [Fact]
    public void APidThatIsNotThereCannotBeOpened()
    {
        // The refusal the capture loop turns into TargetCannotBePinned rather than proceeding to inject.
        HeldProcessHandle.TryOpen(0x7FFFFFFE).Should().BeNull();
    }
}
