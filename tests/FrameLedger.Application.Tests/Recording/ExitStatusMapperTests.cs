using FluentAssertions;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Recording;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Tests.Recording;

/// <summary><c>04_CAPTURE</c> §Crash &amp; exit classification, case by case.</summary>
public sealed class ExitStatusMapperTests
{
    [Theory]
    [InlineData(SessionEndReason.TargetExited, 0, false, ExitStatus.Normal)]
    [InlineData(SessionEndReason.TargetExited, -1073741819, false, ExitStatus.Crashed)]
    [InlineData(SessionEndReason.TargetExited, 0, true, ExitStatus.Crashed)]
    [InlineData(SessionEndReason.TargetExited, null, true, ExitStatus.Crashed)]
    [InlineData(SessionEndReason.Running, null, false, ExitStatus.Normal)]
    [InlineData(SessionEndReason.SupervisionFaulted, null, false, ExitStatus.Normal)]
    [InlineData(SessionEndReason.SafetyUnhook, 0, false, ExitStatus.UnhookedSafety)]
    [InlineData(SessionEndReason.SafetyUnhook, 1, true, ExitStatus.UnhookedSafety)]
    [InlineData(SessionEndReason.SupervisionLost, null, false, ExitStatus.Degraded)]
    [InlineData(SessionEndReason.WriterSelfDisabled, null, false, ExitStatus.Degraded)]
    [InlineData(SessionEndReason.WriterStoppedBlocklisted, null, false, ExitStatus.Degraded)]
    [InlineData(SessionEndReason.WriterNeverInstalledHooks, null, false, ExitStatus.Degraded)]
    [InlineData(SessionEndReason.RefusedByGuard, null, false, ExitStatus.Normal)]
    public void MapsTheReasonTheExitCodeAndTheWitness(SessionEndReason reason, int? exitCode, bool witness, ExitStatus expected)
    {
        ExitStatusMapper.Map(reason, exitCode, witness).Should().Be(expected);
    }

    [Fact]
    public void TheNoteCarriesTheFineReasonTheCodeAndTheWitness()
    {
        ExitStatusMapper.Describe(SessionEndReason.TargetExited, -1073741819, true)
            .Should().Be("end=TargetExited; exit_code=-1073741819; crash_event=application_log");
        ExitStatusMapper.Describe(SessionEndReason.Running, null, false).Should().Be("end=Running");
        ExitStatusMapper.CrashWitnessGrace.Should().Be(TimeSpan.FromSeconds(30));
    }
}
