using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Application.Capture;
using FrameLedger.Application.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;
using FrameLedger.Infrastructure.Persistence;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Capture;

/// <summary>
/// Launch mode's ordering (P1 item 2): the host starts what it was asked to start, consent is the gate's
/// and only the gate's, and the guard is reached through the WAITING entry with the budget, never the plain one.
/// </summary>
public sealed class CaptureSessionLaunchTests : IAsyncDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-launch-" + Guid.NewGuid().ToString("N"));

    private LedgerDatabase? _db;

    private const string _exe = @"C:\Games\Title\game.exe";
    private const string _disclosure = "operator-disclosure/test";
    private const int _pid = 5151;

    private static ExecutableFingerprint Fingerprint =>
        new() { ExePath = _exe, SizeBytes = 90_000, MtimeUnixMs = 1_700_000_000_000 };

    public async ValueTask DisposeAsync()
    {
        if (_db is not null)
        {
            await _db.DisposeAsync().ConfigureAwait(false);
        }

        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private sealed class WaitingGuard : IAntiCheatGuard
    {
        public int InjectCalls { get; private set; }

        public int WhenReadyCalls { get; private set; }

        public int WhenReadyWaitMs { get; private set; }

        public AntiCheatVerdict Verdict { get; set; } = AntiCheatVerdict.Allowed();

        public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default) =>
            ValueTask.FromResult(AntiCheatVerdict.Allowed());

        public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
            CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(Verdict);
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectWhenReadyAsync(int targetPid, string payloadPath,
            int timeoutMs, CancellationToken ct = default)
        {
            WhenReadyCalls++;
            WhenReadyWaitMs = timeoutMs;
            return ValueTask.FromResult(Verdict);
        }

        public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
            CancellationToken ct = default) => ValueTask.FromResult(AntiCheatVerdict.Allowed());
    }

    private sealed class NoLiveness : ITargetLiveness
    {
        public bool HasExited => false;

        public bool IsForeground => true;

        public void Dispose()
        {
        }
    }

    private sealed class NeverResolves : ITargetResolver
    {
        public int? Resolve(string normalisedExePath, out SessionEndReason reason)
        {
            reason = SessionEndReason.TargetNotRunning;
            return null;
        }
    }

    private async Task<IGameConsentStore> StoreAsync(bool consented)
    {
        // The same SQLite adapter the host and the Agent use, on a scratch ledger under this test's directory.
        _db ??= await LedgerDatabase.OpenAsync(Path.Combine(_dir, LedgerPaths.DatabaseFileName), ct: TestContext.Current.CancellationToken)
            .ConfigureAwait(false);
        var store = new SqliteGameConsentStore(_db);
        if (consented)
        {
            await store.RecordOperatorAcknowledgementAsync(new OperatorAcknowledgement
            {
                Fingerprint = Fingerprint,
                DisclosureVersion = _disclosure,
                AcknowledgedAt = DateTimeOffset.UnixEpoch,
            }, TestContext.Current.CancellationToken).ConfigureAwait(false);
        }

        return store;
    }

    private static CaptureSession Loop(IGameConsentStore store, WaitingGuard guard,
        IProcessLauncher? launcher) =>
        new(store,
            new HookedCaptureGate(guard),
            guard,
            new NeverResolves(),
            new DelegateLivenessSource(_ => new NoLiveness()),
            new DelegateRingAttacher(_ => (null, ShmAttachRefusal.BuildIdMismatch)),
            new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromMilliseconds(20),
                MaxDuration = TimeSpan.FromMilliseconds(50),
                LaunchWaitBudget = TimeSpan.FromSeconds(42),
                LogFlushGrace = TimeSpan.FromMilliseconds(1),
            },
            launcher: launcher);

    [Fact]
    public async Task AConsentedLaunchReachesTheGuardThroughTheWaitingEntryWithTheBudget()
    {
        var guard = new WaitingGuard();
        string? launchedWith = null;
        CaptureSession loop = Loop(await StoreAsync(consented: true), guard, new DelegateProcessLauncher((exe, args) =>
        {
            launchedWith = exe + " | " + args;
            return (_pid, new NoLiveness());
        }));

        CaptureOutcome r = await loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", "--real --hold 3",
            TestContext.Current.CancellationToken);

        launchedWith.Should().Be(_exe + " | --real --hold 3");
        guard.WhenReadyCalls.Should().Be(1);
        guard.WhenReadyWaitMs.Should().Be(42_000, "the loop's budget is what the guard polls against");
        guard.InjectCalls.Should().Be(0, "launch mode never takes the plain entry, which would inject on sight");
        r.LaunchWait.Should().NotBeNull("the report prints how long the guard waited before it injected");
        r.Reason.Should().Be(SessionEndReason.AttachRefused, "this fixture has no ring; the guard's answer is what is under test");
    }

    [Fact]
    public async Task ALaunchThatCannotStartIsItsOwnReasonAndTheGuardIsNeverAsked()
    {
        var guard = new WaitingGuard();
        CaptureSession loop = Loop(await StoreAsync(consented: true), guard, new DelegateProcessLauncher((_, _) => null));

        CaptureOutcome r = await loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", string.Empty,
            TestContext.Current.CancellationToken);

        r.Reason.Should().Be(SessionEndReason.LaunchCannotStart);
        guard.WhenReadyCalls.Should().Be(0);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task WithoutConsentTheProcessIsStillStartedAndTheGateRefusesBeforeTheGuard()
    {
        // The operator asked for the game to start; consent decides whether FrameLedger goes INTO it, and
        // that decision is the gate's alone — reached one process later than in attach mode, never skipped.
        var guard = new WaitingGuard();
        int launches = 0;
        CaptureSession loop = Loop(await StoreAsync(consented: false), guard, new DelegateProcessLauncher((_, _) =>
        {
            launches++;
            return (_pid, new NoLiveness());
        }));

        CaptureOutcome r = await loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", string.Empty,
            TestContext.Current.CancellationToken);

        launches.Should().Be(1);
        r.Reason.Should().Be(SessionEndReason.RefusedHookNotEnabled);
        guard.WhenReadyCalls.Should().Be(0);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task TheGuardsTwoLaunchRefusalsAreTheirOwnSessionEndReasons()
    {
        foreach ((AntiCheatRefusalReason native, SessionEndReason expected) in new[]
                 {
                     (AntiCheatRefusalReason.LaunchTargetExited, SessionEndReason.LaunchTargetExited),
                     (AntiCheatRefusalReason.LaunchNoPresentationRuntime, SessionEndReason.LaunchNoPresentationRuntime),
                 })
        {
            var guard = new WaitingGuard { Verdict = AntiCheatVerdict.Refused(native, string.Empty, "poll ended") };
            CaptureSession loop = Loop(await StoreAsync(consented: true), guard, new DelegateProcessLauncher((_, _) => (_pid, new NoLiveness())));

            CaptureOutcome r = await loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", string.Empty,
                TestContext.Current.CancellationToken);

            r.Reason.Should().Be(expected);
            r.Verdict.Reason.Should().Be(native);
        }
    }

    [Fact]
    public async Task AVulkanLayeredVerdictIsNeitherAllowNorRefusalAndTheLoopAttachesToTheLayersRing()
    {
        // P1 item 3: the guard passed, injected nothing, and the ring is the layer's -- so the loop goes
        // on to attach exactly as it would after an injection, carrying the verdict for the report.
        var guard = new WaitingGuard
        {
            Verdict = AntiCheatVerdict.Refused(AntiCheatRefusalReason.TargetIsVulkanLayered, string.Empty, "vulkan-1 only"),
        };
        int attachCalls = 0;
        var loop = new CaptureSession(await StoreAsync(consented: true), new HookedCaptureGate(guard), guard,
            new NeverResolves(), new DelegateLivenessSource(_ => new NoLiveness()),
            new DelegateRingAttacher(_ =>
            {
                attachCalls++;
                return (null, ShmAttachRefusal.BuildIdMismatch);
            }),
            new CaptureOptions
            {
                DrainInterval = TimeSpan.FromMilliseconds(1),
                ScanInterval = TimeSpan.FromMilliseconds(5),
                AttachBudget = TimeSpan.FromMilliseconds(20),
                MaxDuration = TimeSpan.FromMilliseconds(50),
                LaunchWaitBudget = TimeSpan.FromSeconds(42),
                LogFlushGrace = TimeSpan.FromMilliseconds(1),
            },
            launcher: new DelegateProcessLauncher((_, _) => (_pid, new NoLiveness())));

        CaptureOutcome r = await loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", string.Empty,
            TestContext.Current.CancellationToken);

        attachCalls.Should().BeGreaterThan(0, "a layered target is attached to, not refused");
        r.Reason.Should().Be(SessionEndReason.AttachRefused, "this fixture has no ring");
        r.Verdict.Reason.Should().Be(AntiCheatRefusalReason.TargetIsVulkanLayered);
        guard.InjectCalls.Should().Be(0);
    }

    [Fact]
    public async Task ALoopBuiltWithoutALauncherCannotLaunch()
    {
        var guard = new WaitingGuard();
        CaptureSession loop = Loop(await StoreAsync(consented: true), guard, launcher: null);

        Func<Task> act = () => loop.RunLaunchedAsync(_exe, Fingerprint, "payload.dll", string.Empty,
            TestContext.Current.CancellationToken);

        await act.Should().ThrowAsync<InvalidOperationException>();
    }
}
