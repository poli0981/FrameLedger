using FluentAssertions;
using FrameLedger.Application.Recording;
using FrameLedger.Domain.Sessions;

namespace FrameLedger.Application.Tests.Recording;

/// <summary><c>19_SAFETY</c> §Crash &amp; stability safety: two crashes within 60 s of injection, and not one more thing.</summary>
public sealed class CrashAutoDisablePolicyTests
{
    private static readonly DateTimeOffset _injected = new(2026, 9, 9, 12, 0, 0, TimeSpan.Zero);

    [Fact]
    public void ACrashSixtyOneSecondsAfterInjectionIsNotAnEarlyCrash()
    {
        CrashAutoDisablePolicy.IsEarlyCrash(ExitStatus.Crashed, _injected, _injected.AddSeconds(61)).Should().BeFalse();
        CrashAutoDisablePolicy.IsEarlyCrash(ExitStatus.Crashed, _injected, _injected.AddSeconds(60)).Should().BeTrue();
        CrashAutoDisablePolicy.IsEarlyCrash(ExitStatus.Normal, _injected, _injected.AddSeconds(5)).Should().BeFalse();
        CrashAutoDisablePolicy.IsEarlyCrash(ExitStatus.Crashed, null, _injected.AddSeconds(5)).Should().BeFalse("nothing was injected");
    }

    [Fact]
    public async Task TheSecondEarlyCrashDisablesHookingWithTheReasonOnTheRow()
    {
        var games = new FakeGameRepository();
        var policy = new CrashAutoDisablePolicy(games);

        CrashPolicyOutcome first = await policy.ApplyAsync(1, ExitStatus.Crashed, _injected, _injected.AddSeconds(10), TestContext.Current.CancellationToken);
        CrashPolicyOutcome late = await policy.ApplyAsync(1, ExitStatus.Crashed, _injected, _injected.AddSeconds(61), TestContext.Current.CancellationToken);
        CrashPolicyOutcome second = await policy.ApplyAsync(1, ExitStatus.Crashed, _injected, _injected.AddSeconds(59), TestContext.Current.CancellationToken);

        first.Should().Be(CrashPolicyOutcome.Counted);
        late.Should().Be(CrashPolicyOutcome.NotAnEarlyCrash, "61 s after injection is the game's own crash");
        second.Should().Be(CrashPolicyOutcome.HookingDisabled);
        games.CrashCount.Should().Be(2, "only the early ones were counted");
        games.Disabled.Should().ContainSingle().Which.Reason.Should().Be(CrashAutoDisablePolicy.Reason);
    }
}
