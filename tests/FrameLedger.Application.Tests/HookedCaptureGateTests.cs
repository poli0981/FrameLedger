using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Application.Tests;

public sealed class HookedCaptureGateTests
{
    private sealed class RecordingGuard : IAntiCheatGuard
    {
        public int InjectCalls { get; private set; }
        public int EvaluateCalls { get; private set; }
        public AntiCheatVerdict Next { get; set; } = AntiCheatVerdict.Allowed();

        public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default)
        {
            EvaluateCalls++;
            return ValueTask.FromResult(Next);
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
            CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(Next);
        }

        public int PreScanCalls { get; private set; }

        public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
            CancellationToken ct = default)
        {
            PreScanCalls++;
            return ValueTask.FromResult(Next);
        }
    }

    // `withConsent: false` is the only way to express ABSENT consent, and it had
    // to be added: `consent ?? DateTimeOffset.UnixEpoch` meant passing
    // `consent: null` produced a request that HAD consented, so the case FR-2.1
    // exists for was inexpressible in this fixture. Same shape as §S18's
    // FakeEnumModules, which ignored its pid and made the arrangement §S16 was
    // written to catch impossible to write down.
    private static HookRequest Request(bool enabled = true, DateTimeOffset? consent = null,
        string? blocked = null, bool withConsent = true) =>
        new()
        {
            TargetPid = 1234,
            PayloadPath = @"C:\FrameLedger\FrameLedger.Overlay.dll",
            HookEnabled = enabled,
            ConsentedAt = withConsent ? consent ?? DateTimeOffset.UnixEpoch : null,
            BlockedReason = blocked,
        };

    [Fact]
    public async Task AnEnabledConsentedGame_ReachesTheGuard()
    {
        RecordingGuard guard = new();
        HookedCaptureGate gate = new(guard);

        AntiCheatVerdict v = await gate.StartAsync(Request(), TestContext.Current.CancellationToken);

        v.IsAllowed.Should().BeTrue();
        guard.InjectCalls.Should().Be(1);
    }

    [Fact]
    public async Task HookingOff_NeverReachesTheGuard()
    {
        // CLAUDE.md rule 1: nothing is injected because a game was merely
        // added. The guard is not even asked.
        RecordingGuard guard = new();
        HookedCaptureGate gate = new(guard);

        AntiCheatVerdict v = await gate.StartAsync(Request(enabled: false), TestContext.Current.CancellationToken);

        v.IsAllowed.Should().BeFalse();
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task NoConsent_NeverReachesTheGuard()
    {
        RecordingGuard guard = new();
        HookedCaptureGate gate = new(guard);

        AntiCheatVerdict v = await gate.StartAsync(Request() with { ConsentedAt = null }, TestContext.Current.CancellationToken);

        v.IsAllowed.Should().BeFalse();
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task APreviouslyBlockedGame_NeverReachesTheGuard()
    {
        // 19_SAFETY: a game enabled before it started matching is force-disabled
        // on the next rules update. Honouring hook_blocked_reason here means a
        // stale in-memory watchlist cannot resurrect it.
        RecordingGuard guard = new();
        HookedCaptureGate gate = new(guard);

        AntiCheatVerdict v = await gate.StartAsync(Request(blocked: "EasyAntiCheat/ appeared in the game directory"), TestContext.Current.CancellationToken);

        v.IsAllowed.Should().BeFalse();
        v.Signal.Should().Contain("EasyAntiCheat");
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AGuardRefusal_IsPassedThroughUnchanged()
    {
        // The gate adds no judgement of its own about anti-cheat. Whatever the
        // guard said is what the caller sees.
        RecordingGuard guard = new()
        {
            Next = AntiCheatVerdict.Refused(AntiCheatRefusalReason.BlockedDriver, "Riot Vanguard", "vgk.sys"),
        };
        HookedCaptureGate gate = new(guard);

        AntiCheatVerdict v = await gate.StartAsync(Request(), TestContext.Current.CancellationToken);

        v.Reason.Should().Be(AntiCheatRefusalReason.BlockedDriver);
        v.Family.Should().Be("Riot Vanguard");
    }

    [Fact]
    public async Task ShouldUnhook_IsTrueForAnyRefusal()
    {
        RecordingGuard guard = new()
        {
            Next = AntiCheatVerdict.Refused(AntiCheatRefusalReason.BlockedModule, "BattlEye", "BEClient_x64.dll"),
        };
        HookedCaptureGate gate = new(guard);

        (await gate.ShouldUnhookAsync(1234, TestContext.Current.CancellationToken)).Should().BeTrue();
        guard.EvaluateCalls.Should().Be(1);
    }

    [Fact]
    public async Task ShouldUnhook_IsFalseOnlyWhenAllowed()
    {
        RecordingGuard guard = new();
        HookedCaptureGate gate = new(guard);

        (await gate.ShouldUnhookAsync(1234, TestContext.Current.CancellationToken)).Should().BeFalse();
    }

    // Each managed refusal must be DISTINGUISHABLE. All three used to return
    // BlockedExecutable — check 3's code, which the native guard cannot produce
    // at all (§S14: the matchers have no call site), so a user who had not
    // accepted the consent dialog would have been told the title was on the
    // per-title blocklist. Nothing asserted the reason, which is why nothing
    // noticed.
    [Fact]
    public async Task EachManagedRefusal_CarriesItsOwnReason()
    {
        var guard = new RecordingGuard();
        var gate = new HookedCaptureGate(guard);

        CancellationToken ct = TestContext.Current.CancellationToken;
        AntiCheatVerdict notEnabled = await gate.StartAsync(Request(enabled: false), ct);
        AntiCheatVerdict noConsent = await gate.StartAsync(Request(withConsent: false), ct);
        AntiCheatVerdict blocked = await gate.StartAsync(Request(blocked: "EasyAntiCheat appeared after a patch"), ct);

        notEnabled.Reason.Should().Be(AntiCheatRefusalReason.HookNotEnabled);
        noConsent.Reason.Should().Be(AntiCheatRefusalReason.ConsentMissing);
        blocked.Reason.Should().Be(AntiCheatRefusalReason.PreviouslyBlocked);

        // ...and none of them borrows a reason the native guard owns.
        new[] { notEnabled.Reason, noConsent.Reason, blocked.Reason }
            .Should().OnlyHaveUniqueItems()
            .And.NotContain(AntiCheatRefusalReason.BlockedExecutable);

        guard.InjectCalls.Should().Be(0, "a refusal here must never reach the guard, let alone the primitive");
    }

    [Fact]
    public void ANullGuard_IsRejected()
    {
        Action act = () => _ = new HookedCaptureGate(null!);
        act.Should().Throw<ArgumentNullException>();
    }
}
