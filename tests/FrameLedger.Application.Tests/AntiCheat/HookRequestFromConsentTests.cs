using System.Reflection;
using FluentAssertions;
using FrameLedger.Application.AntiCheat;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;

namespace FrameLedger.Application.Tests.AntiCheat;

/// <summary>
/// The one path from a stored record to the gate, driven THROUGH the gate.
/// </summary>
/// <remarks>
/// Every case asserts the gate's own outcome and whether the guard was reached,
/// because the property that matters is not what the factory returns — it is that
/// <c>FlGuardedInject</c> is never called for a game nobody consented to
/// (<c>docs/HANDOFF.md</c>: "Assert <c>ConsentMissing</c> with <c>FlGuardedInject</c>
/// never reached").
/// </remarks>
public sealed class HookRequestFromConsentTests
{
    private sealed class RecordingGuard : IAntiCheatGuard
    {
        public int InjectCalls { get; private set; }

        public ValueTask<AntiCheatVerdict> EvaluateAsync(int targetPid, CancellationToken ct = default) =>
            ValueTask.FromResult(AntiCheatVerdict.Allowed());

        public ValueTask<AntiCheatVerdict> GuardedInjectAsync(int targetPid, string payloadPath,
            CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(AntiCheatVerdict.Allowed());
        }

        public ValueTask<AntiCheatVerdict> GuardedInjectWhenReadyAsync(int targetPid, string payloadPath,
            int timeoutMs, CancellationToken ct = default)
        {
            InjectCalls++;
            return ValueTask.FromResult(AntiCheatVerdict.Allowed());
        }

        public ValueTask<AntiCheatVerdict> PreScanGameDirectoryAsync(string gameDirectory,
            CancellationToken ct = default) => ValueTask.FromResult(AntiCheatVerdict.Allowed());
    }

    private const string _payload = @"C:\FrameLedger\FrameLedger.Overlay.dll";

    private static ExecutableFingerprint OnDisk =>
        new() { ExePath = @"C:\Games\Title\game.exe", SizeBytes = 90_000, MtimeUnixMs = 1_700_000_000_000 };

    private static GameConsentRecord Consented(
        ExecutableFingerprint? fingerprint = null,
        bool hookEnabled = true,
        ConsentProvenance provenance = ConsentProvenance.UnshippedHostOperator,
        string? blockedReason = null) =>
        GameConsentRecord.Stored(
            fingerprint ?? OnDisk, hookEnabled, DateTimeOffset.UnixEpoch, provenance,
            "unshipped-host-operator/1", blockedReason, preScanUnverified: false,
            updatedAt: DateTimeOffset.UnixEpoch);

    private static async Task<(AntiCheatVerdict Verdict, int InjectCalls)> ThroughTheGateAsync(
        GameConsentRecord record)
    {
        RecordingGuard guard = new();
        HookRequest request = HookRequest.FromConsent(record, OnDisk, targetPid: 4242, _payload);
        AntiCheatVerdict verdict = await new HookedCaptureGate(guard)
            .StartAsync(request, TestContext.Current.CancellationToken)
            .ConfigureAwait(false);
        return (verdict, guard.InjectCalls);
    }

    [Fact]
    public void SynthesisingTheGatesInputsDoesNotCompileAndTheSurfaceIsPinned()
    {
        // §S27 named and rejected `HookEnabled = true, ConsentedAt = UtcNow` synthesised at a call site:
        // "It compiles. It is a gate whose verdict is decided before it looks." It compiled here too,
        // because HookRequest was a record with `required`/`init` members — a store and a provenance flag
        // only added an honest path BESIDE the dishonest one.
        //
        // This asserts the thing that closed it: there is no public constructor and no public setter, so
        // the expression §S27 quotes is not a discouraged idiom but a compile error. The canary is
        // re-adding either.
        typeof(HookRequest).GetConstructors(BindingFlags.Public | BindingFlags.Instance)
            .Should().BeEmpty("the only way to build a request is from a stored consent record");

        typeof(HookRequest).GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.SetMethod is { IsPublic: true })
            .Select(p => p.Name)
            .Should().BeEmpty();

        typeof(HookRequest).GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Select(m => m.Name)
            .Should().BeEquivalentTo([nameof(HookRequest.FromConsent)]);
    }

    [Fact]
    public async Task AnAbsentRecordIsHookNotEnabledAndTheGuardIsNeverAsked()
    {
        // The refusal HANDOFF names for item 1's acceptance, reached from the value a store returns when
        // it has nothing: default(GameConsentRecord). HookNotEnabled rather than ConsentMissing, because
        // the gate checks enablement first and "a game was merely added" is the more accurate sentence.
        (AntiCheatVerdict verdict, int injectCalls) = await ThroughTheGateAsync(default);

        verdict.Reason.Should().Be(AntiCheatRefusalReason.HookNotEnabled);
        injectCalls.Should().Be(0, "nothing may reach the injection primitive without a consent record");
    }

    [Fact]
    public async Task AConsentTimestampWithNoDisclosureProvenanceStillRefuses()
    {
        // games.hook_consent_at is a bare timestamp and cannot say what was disclosed. A record carrying
        // a time with ConsentProvenance.NotRecorded is one nobody was shown anything for, and trusting
        // the timestamp because it is non-null is the gap ConsentProvenance exists to close.
        (AntiCheatVerdict verdict, int injectCalls) =
            await ThroughTheGateAsync(Consented(provenance: ConsentProvenance.NotRecorded));

        verdict.Reason.Should().Be(AntiCheatRefusalReason.ConsentMissing);
        injectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AStaleFingerprintRefusesThisSessionAndLeavesTheStoredConsentAlone()
    {
        // 19_SAFETY: hook_consent_at is PRESERVED — the user did consent, and a title changing under
        // them is not a withdrawal. So the refusal is for this session and the record is untouched.
        GameConsentRecord stored = Consented(OnDisk with { SizeBytes = 12 });

        (AntiCheatVerdict verdict, int injectCalls) = await ThroughTheGateAsync(stored);

        verdict.Reason.Should().Be(AntiCheatRefusalReason.ConsentMissing);
        injectCalls.Should().Be(0);
        stored.ConsentedAt.Should().Be(DateTimeOffset.UnixEpoch, "the factory must not write to the store");
    }

    [Fact]
    public async Task HookingOffRefusesEvenWithGoodConsent()
    {
        (AntiCheatVerdict verdict, int injectCalls) = await ThroughTheGateAsync(Consented(hookEnabled: false));

        verdict.Reason.Should().Be(AntiCheatRefusalReason.HookNotEnabled);
        injectCalls.Should().Be(0);
    }

    [Fact]
    public async Task ABlockedReasonIsCarriedThroughSoTheGateCanRefuseOnIt()
    {
        (AntiCheatVerdict verdict, int injectCalls) =
            await ThroughTheGateAsync(Consented(blockedReason: "BlockedModule: BattlEye BEClient_x64.dll"));

        verdict.Reason.Should().Be(AntiCheatRefusalReason.PreviouslyBlocked);
        verdict.Signal.Should().Contain("BattlEye");
        injectCalls.Should().Be(0);
    }

    [Fact]
    public async Task AValidRecordReachesTheGuardExactlyOnce()
    {
        // GREEN FIRST. Every assertion above is satisfied by a factory that refuses everything, so the
        // suite needs one case that must PASS through or it proves only that nothing works.
        (AntiCheatVerdict verdict, int injectCalls) = await ThroughTheGateAsync(Consented());

        verdict.IsAllowed.Should().BeTrue();
        injectCalls.Should().Be(1);
    }

    [Fact]
    public void ThePayloadPathIsRequired()
    {
        Action act = () => HookRequest.FromConsent(Consented(), OnDisk, 1, "  ");
        Action negativeWait = () => HookRequest.FromConsent(Consented(), OnDisk, 1, _payload, waitForPresentationRuntimeMs: -1);
        negativeWait.Should().Throw<ArgumentOutOfRangeException>("a negative budget is a mistake, never attach mode");
        HookRequest.FromConsent(Consented(), OnDisk, 1, _payload).WaitForPresentationRuntimeMs
            .Should().Be(0, "attach mode is the default: inject now");
        act.Should().Throw<ArgumentException>();
    }
}
