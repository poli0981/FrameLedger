using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;

namespace FrameLedger.Application.Tests;

/// <summary>
/// The supervision tick must be evidence that a scan HAPPENED, not that this
/// process is alive.
/// </summary>
/// <remarks>
/// <c>20_OPEN_QUESTIONS</c> §S2, part three. The capture side decides whether to
/// keep observing by watching this counter, so a tick that advances while the
/// guard loop is dead is a gate incapable of going red for the reason it exists
/// — the defect class this project keeps finding. Every test below forces one
/// of the ways a scan can fail to complete and asserts the counter does not move.
/// </remarks>
public sealed class GuardSupervisorTests
{
    private sealed class ScriptedGuard : IAntiCheatGuard
    {
        public Func<AntiCheatVerdict>? OnEvaluate { get; set; }
        public int Calls { get; private set; }

        public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default)
        {
            Calls++;
            ct.ThrowIfCancellationRequested();
            return ValueTask.FromResult(OnEvaluate is null ? AntiCheatVerdict.Allowed() : OnEvaluate());
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
            CancellationToken ct = default) =>
            ValueTask.FromResult(AntiCheatVerdict.Allowed());

        // The supervisor never asks check 4 anything: it re-evaluates a process
        // it is already inside, and the pre-scan is the pre-launch question.
        public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
            CancellationToken ct = default) =>
            throw new InvalidOperationException("the supervisor must not call the pre-scan");
    }

    [Fact]
    public async Task ACompletedCleanScan_AdvancesTheTick()
    {
        // The green direction. Without it a deadline so tight that the capture
        // side is always inert would pass every other test here.
        GuardSupervisor supervisor = new(new ScriptedGuard());

        (await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken)).Should().BeTrue();

        supervisor.CompletedEvaluations.Should().Be(1);
        supervisor.UnhookRequested.Should().BeFalse();
    }

    [Fact]
    public async Task AGuardThatTHROWS_DoesNotAdvanceTheTick()
    {
        // The whole point. A supervisor that caught this and carried on would
        // keep publishing "supervised" while scanning nothing.
        ScriptedGuard guard = new() { OnEvaluate = () => throw new InvalidOperationException("scan blew up") };
        GuardSupervisor supervisor = new(guard);

        Func<Task> act = async () => await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken).ConfigureAwait(false);

        await act.Should().ThrowAsync<InvalidOperationException>();
        supervisor.CompletedEvaluations.Should().Be(0);
    }

    [Fact]
    public async Task ACancelledScan_DoesNotAdvanceTheTick()
    {
        using CancellationTokenSource cts = new();
        await cts.CancelAsync();
        GuardSupervisor supervisor = new(new ScriptedGuard());

        Func<Task> act = async () => await supervisor.ScanOnceAsync(1234, cts.Token).ConfigureAwait(false);

        await act.Should().ThrowAsync<OperationCanceledException>();
        supervisor.CompletedEvaluations.Should().Be(0);
    }

    [Fact]
    public async Task ARefusal_AdvancesTheTickAndRequestsUnhook()
    {
        // A refusal IS a completed evaluation — the scan ran and produced an
        // answer. The unhook flag is what carries the verdict; the counter must
        // not be overloaded to mean two things.
        ScriptedGuard guard = new()
        {
            OnEvaluate = () => AntiCheatVerdict.Refused(
                AntiCheatRefusalReason.BlockedDriver, "Riot Vanguard", "vgk.sys"),
        };
        GuardSupervisor supervisor = new(guard);

        (await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken)).Should().BeFalse();

        supervisor.CompletedEvaluations.Should().Be(1);
        supervisor.UnhookRequested.Should().BeTrue();
        supervisor.LastVerdict!.Value.Reason.Should().Be(AntiCheatRefusalReason.BlockedDriver);
    }

    [Fact]
    public async Task OnceRefused_ItLatchesAndStopsScanning()
    {
        // A later clean scan must not resurrect the session, and must not let
        // the tick keep advancing while capture is supposed to be stopping.
        ScriptedGuard guard = new()
        {
            OnEvaluate = () => AntiCheatVerdict.Refused(
                AntiCheatRefusalReason.BlockedModule, "BattlEye", "BEClient_x64.dll"),
        };
        GuardSupervisor supervisor = new(guard);

        await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken);
        guard.OnEvaluate = AntiCheatVerdict.Allowed;

        (await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken)).Should().BeFalse();

        supervisor.UnhookRequested.Should().BeTrue();
        supervisor.CompletedEvaluations.Should().Be(1, "the latched supervisor must not keep ticking");
        guard.Calls.Should().Be(1, "it must not even ask again");
    }

    [Fact]
    public async Task RepeatedCleanScans_EachAdvanceTheTickExactlyOnce()
    {
        GuardSupervisor supervisor = new(new ScriptedGuard());

        for (int i = 1; i <= 5; i++)
        {
            await supervisor.ScanOnceAsync(1234, TestContext.Current.CancellationToken);
            supervisor.CompletedEvaluations.Should().Be((uint)i);
        }
    }

    [Fact]
    public void ANullGuard_IsRejected()
    {
        Action act = () => _ = new GuardSupervisor(null!);
        act.Should().Throw<ArgumentNullException>();
    }

    [Fact]
    public void ABrandNewSupervisor_HasNotSupervisedAnything()
    {
        // "Never advanced" and "stopped advancing" are the same state to a
        // consumer, and both mean unsupervised. A capture side that is never
        // adopted must be inert from the beginning, not after a grace window.
        GuardSupervisor supervisor = new(new ScriptedGuard());

        supervisor.CompletedEvaluations.Should().Be(0);
        supervisor.LastVerdict.Should().BeNull();
        supervisor.UnhookRequested.Should().BeFalse();
    }
}
