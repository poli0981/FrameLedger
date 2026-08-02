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
    }

    private static HookRequest Request(bool enabled = true, DateTimeOffset? consent = null,
        string? blocked = null) =>
        new()
        {
            TargetPid = 1234,
            PayloadPath = @"C:\FrameLedger\FrameLedger.Overlay.dll",
            HookEnabled = enabled,
            ConsentedAt = consent ?? DateTimeOffset.UnixEpoch,
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

    [Fact]
    public void ANullGuard_IsRejected()
    {
        Action act = () => _ = new HookedCaptureGate(null!);
        act.Should().Throw<ArgumentNullException>();
    }
}
