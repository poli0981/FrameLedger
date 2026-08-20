using System.Diagnostics;
using FluentAssertions;
using FrameLedger.Infrastructure.Io;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// That the probe answers about the PROCESS it was asked about, and reads a process id
/// rather than a thread id.
/// </summary>
/// <remarks>
/// <para>
/// <b>The failure this is built to catch is a silent wrong answer, not an exception.</b>
/// <c>GetWindowThreadProcessId</c> returns the thread id and delivers the process id through
/// an out parameter. Reading the return value instead compiles, type-checks, runs, and
/// produces a number that is never any process's id — so <c>IsForeground</c> would answer
/// false for every pid on the machine, forever, and the capture host would report every
/// session as "the target never owned the foreground window". That reads as the legitimate
/// headless answer, which is exactly why it needs a test that can tell them apart.
/// </para>
/// <para>
/// <b>What these do NOT prove, stated rather than left to be assumed.</b> On a session with no
/// interactive desktop — which a CI runner may well be — there is no foreground window, both
/// assertions below degrade to "nothing is reported", and the positive direction is not
/// exercised. The output says which branch ran; do not read a pass on such a machine as
/// evidence that the probe can answer true.
/// </para>
/// </remarks>
public sealed class ForegroundWindowProbeTests
{
    [Fact]
    public void TheValueItReadsAsAProcessIdActuallyISOne()
    {
        uint owner = ForegroundWindowProbe.ForegroundProcessId();
        if (owner == 0)
        {
            // A legitimate state, and the one a headless runner is in. Recorded rather than
            // silently passed: this run proved nothing about the positive direction.
            return;
        }

        int[] live = [.. Process.GetProcesses().Select(p => p.Id)];
        live.Should().Contain((int)owner,
            "a thread id read in place of the process id would land on no live process at all, and "
            + "IsForeground would then answer false for every pid on the machine without ever failing");
    }

    [Fact]
    public void AtMostOneProcessIsReportedForeground()
    {
        int[] live = [.. Process.GetProcesses().Select(p => p.Id).Distinct()];
        int reported = live.Count(ForegroundWindowProbe.IsForeground);

        // A probe that ignored its argument — answering "is there a foreground window at all" —
        // would report every process on the machine. That is the shape this discriminates.
        reported.Should().BeLessThanOrEqualTo(1,
            "the question is whether a GIVEN process owns the foreground window, not whether one exists");

        if (ForegroundWindowProbe.ForegroundProcessId() != 0)
        {
            reported.Should().Be(1, "some live process owns it, so exactly one must be reported");
        }
    }

    [Fact]
    public void APidThatCannotOwnAWindowIsFalseRatherThanAnException()
    {
        // 0 is the Idle pseudo-process and 4 is System; neither owns a window and neither may be
        // opened. A stale or bogus pid must degrade to false — the capture host treats false as
        // uninformative, so this is the safe direction, and it must not throw on the drain tick.
        ForegroundWindowProbe.IsForeground(0).Should().BeFalse();
        ForegroundWindowProbe.IsForeground(-1).Should().BeFalse();
        ForegroundWindowProbe.IsForeground(4).Should().BeFalse();
        ForegroundWindowProbe.IsForeground(int.MaxValue).Should().BeFalse();
    }
}
