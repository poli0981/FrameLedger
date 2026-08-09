using FluentAssertions;
using FrameLedger.Application.Consent;
using FrameLedger.CaptureHost.Consent;
using FrameLedger.Domain.AntiCheat;
using FrameLedger.Domain.Consent;

namespace FrameLedger.CaptureHost.Tests.Consent;

public sealed class FileGameConsentStoreTests : IDisposable
{
    // A scratch directory, never the real location. The real one is beside the host binary, and a test
    // that wrote there would leave a consent record on the machine that ran the suite.
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-consent-" + Guid.NewGuid().ToString("N"));

    private FileGameConsentStore Store => new(Path.Combine(_dir, "games.json"));

    private static ExecutableFingerprint Fingerprint(string path = @"C:\Games\Title\game.exe") =>
        new() { ExePath = path, SizeBytes = 90_000, MtimeUnixMs = 1_700_000_000_000 };

    private static OperatorAcknowledgement Ack(ExecutableFingerprint? fp = null) => new()
    {
        Fingerprint = fp ?? Fingerprint(),
        DisclosureVersion = OperatorDisclosure.Version,
        AcknowledgedAt = DateTimeOffset.UnixEpoch,
    };

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (DirectoryNotFoundException)
        {
            // Nothing was written. Nothing to clean.
        }
    }

    [Fact]
    public void TheProductLocationIsBesideTheUnshippedHostAndNotTheAgentsDirectory()
    {
        // src/FrameLedger.Agent/Program.cs records as ratified (§S18 blocker 3) that the Agent is the
        // sole owner of %LOCALAPPDATA%\FrameLedger "and therefore the only thing that may write there".
        // A consent record is also a WIDENING input, unlike the rules file which can only narrow, so
        // confining it to a build tree bounds the blast radius to the machine that built the host.
        var store = new FileGameConsentStore();

        store.Destination.Should().StartWith(AppContext.BaseDirectory);
        Path.GetFullPath(store.Destination).Should().NotStartWith(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
    }

    [Fact]
    public async Task AnAbsentRecordIsTheRefusingDefault()
    {
        GameConsentRecord r = await Store.FindAsync(@"C:\Games\Nothing\here.exe", TestContext.Current.CancellationToken);

        r.IsFromStore.Should().BeFalse();
        r.HookEnabled.Should().BeFalse();
        r.ConsentedAt.Should().BeNull();
    }

    [Fact]
    public async Task AnAcknowledgementRoundTripsWithItsProvenanceAndDisclosureVersion()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;

        (await store.RecordOperatorAcknowledgementAsync(Ack(), ct)).Should().Be(ConsentWriteOutcome.Written);
        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);

        r.IsFromStore.Should().BeTrue();
        r.HookEnabled.Should().BeTrue();
        r.ConsentedAt.Should().Be(DateTimeOffset.UnixEpoch);
        r.Provenance.Should().Be(ConsentProvenance.UnshippedHostOperator);
        r.DisclosureVersion.Should().Be(OperatorDisclosure.Version);
        r.Fingerprint.SizeBytes.Should().Be(90_000);
    }

    [Fact]
    public async Task AFilenameMatchIsNotAConsentMatch()
    {
        // 04_CAPTURE permits a filename fallback for the WATCHLIST, where a wrong match costs a
        // stale-path badge. Applied to consent the polarity inverts: a different binary with the same
        // filename would inherit an existing consent record.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(Fingerprint(@"C:\a\game.exe")), ct);

        (await store.FindAsync(@"C:\b\game.exe", ct)).IsFromStore.Should().BeFalse();
        (await store.FindAsync(@"C:\a\game.exe", ct)).IsFromStore.Should().BeTrue();
    }

    [Fact]
    public async Task PathMatchingIsCaseInsensitive()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(Fingerprint(@"C:\Games\Title\game.exe")), ct);

        (await store.FindAsync(@"c:\games\title\GAME.EXE", ct)).IsFromStore.Should().BeTrue();
    }

    [Fact]
    public async Task AGrantCannotClearAnExistingBlock()
    {
        // CLAUDE.md rule 2: there is no "I understand, continue anyway" button, and a grant that
        // discarded a persisted refusal would be one. OperatorAcknowledgement carries neither
        // BlockedReason nor PreScanUnverified, so the merge in the store is the only source for them.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;

        await store.RecordGuardBlockAsync(
            Fingerprint(),
            AntiCheatVerdict.Refused(AntiCheatRefusalReason.BlockedModule, "BattlEye", "BEClient_x64.dll"),
            ct);

        (await store.RecordOperatorAcknowledgementAsync(Ack(), ct)).Should().Be(ConsentWriteOutcome.Written);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.BlockedReason.Should().NotBeNull().And.Contain("BattlEye");
    }

    [Fact]
    public async Task ABlockPreservesTheConsentStampAndForcesTheToggleOff()
    {
        // 19_SAFETY: "hook_consent_at is PRESERVED. The user did consent; the block is not a withdrawal
        // of consent and must not silently require them to consent again if the title is later cleared."
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        await store.RecordGuardBlockAsync(
            Fingerprint(),
            AntiCheatVerdict.Refused(AntiCheatRefusalReason.AntiCheatDirectory, "Easy Anti-Cheat", "EasyAntiCheat/"),
            ct);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.HookEnabled.Should().BeFalse();
        r.ConsentedAt.Should().Be(DateTimeOffset.UnixEpoch);
        r.Provenance.Should().Be(ConsentProvenance.UnshippedHostOperator);
        r.BlockedReason.Should().Contain("Easy Anti-Cheat");
    }

    [Fact]
    public async Task AnAllowingVerdictCannotRecordABlock()
    {
        // Nothing managed authors an anti-cheat fact (§S15, 04_CAPTURE §The guard).
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;

        (await store.RecordGuardBlockAsync(Fingerprint(), AntiCheatVerdict.Allowed(), ct))
            .Should().Be(ConsentWriteOutcome.Failed);
        (await store.FindAsync(Fingerprint().ExePath, ct)).IsFromStore.Should().BeFalse();
    }

    [Fact]
    public async Task ADefaultVerdictRecordsCouldNotVerifyAndNeverABlock()
    {
        // AntiCheatVerdict's default is a refusal so a forgotten assignment cannot read as permission —
        // but a refusal nobody produced has scanned nothing. 05_DETECTION forbids both collapses:
        // folding it into "blocked" disables the toggle with no appeal, and clearing it is a fail-open.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;

        await store.RecordGuardBlockAsync(Fingerprint(), default, ct);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.PreScanUnverified.Should().BeTrue();
        r.BlockedReason.Should().BeNull("a verdict nobody produced is not evidence of anything");
    }

    [Fact]
    public async Task RevokingWithdrawsTheStampSoTheDisclosureIsShownAgain()
    {
        // The opposite of a block, and deliberately so: a block preserves the stamp because the user
        // did consent, while a revoke IS the withdrawal.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        (await store.RevokeAsync(Fingerprint().ExePath, ct)).Should().Be(ConsentWriteOutcome.Written);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.HookEnabled.Should().BeFalse();
        r.ConsentedAt.Should().BeNull();
        r.Provenance.Should().Be(ConsentProvenance.NotRecorded);
    }

    [Fact]
    public async Task RevokingSomethingThatWasNeverGrantedIsNotFound()
    {
        (await Store.RevokeAsync(@"C:\Games\Nothing\here.exe", TestContext.Current.CancellationToken))
            .Should().Be(ConsentWriteOutcome.NotFound);
    }

    [Fact]
    public async Task ListEnabledReturnsOnlyEnabledGames()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(Fingerprint(@"C:\a\one.exe")), ct);
        await store.RecordOperatorAcknowledgementAsync(Ack(Fingerprint(@"C:\a\two.exe")), ct);
        await store.RevokeAsync(@"C:\a\two.exe", ct);

        IReadOnlyList<GameConsentRecord> enabled = await store.ListEnabledAsync(ct);

        enabled.Should().ContainSingle().Which.Fingerprint.ExePath.Should().Be(@"C:\a\one.exe");
    }

    [Fact]
    public async Task AnUnknownFileVersionReadsAsNoRecordsRatherThanAsPermission()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        string path = Path.Combine(_dir, "games.json");
        string json = await File.ReadAllTextAsync(path, ct);
        await File.WriteAllTextAsync(path, json.Replace("\"version\": 1", "\"version\": 99", StringComparison.Ordinal), ct);

        (await store.FindAsync(Fingerprint().ExePath, ct)).IsFromStore.Should().BeFalse(
            "guessing at a shape we do not understand is the one option that could turn an unreadable "
            + "file into permission");
    }

    [Fact]
    public async Task AnUnrecognisedProvenanceNameReadsAsNotRecorded()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        string path = Path.Combine(_dir, "games.json");
        string json = await File.ReadAllTextAsync(path, ct);
        await File.WriteAllTextAsync(path,
            json.Replace(nameof(ConsentProvenance.UnshippedHostOperator), "ConsentDialog", StringComparison.Ordinal), ct);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.Provenance.Should().Be(ConsentProvenance.NotRecorded,
            "a provenance this build does not know is not one it may act on");
    }

    [Fact]
    public async Task CorruptJsonReadsAsNoRecords()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        Directory.CreateDirectory(_dir);
        await File.WriteAllTextAsync(Path.Combine(_dir, "games.json"), "{ this is not json", ct);

        (await store.FindAsync(Fingerprint().ExePath, ct)).IsFromStore.Should().BeFalse();
    }

    [Theory]
    [InlineData("1")]
    [InlineData("42")]
    [InlineData("unshippedhostoperator")]
    public async Task ANumericOrMiscasedProvenanceIsNotADeclaredMember(string planted)
    {
        // Enum.TryParse ALSO PARSES NUMBERS and does not check the result against the declared members,
        // so "1" yielded UnshippedHostOperator and "42" yielded an undeclared value cast to the enum —
        // an end-run around the two-member count that ConsentProvenance's own test pins. The comment in
        // the store claimed the opposite. Case is part of it too: the writer emits nameof().
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        string path = Path.Combine(_dir, "games.json");
        string json = await File.ReadAllTextAsync(path, ct);
        await File.WriteAllTextAsync(path,
            json.Replace(nameof(ConsentProvenance.UnshippedHostOperator), planted, StringComparison.Ordinal), ct);

        (await store.FindAsync(Fingerprint().ExePath, ct)).Provenance
            .Should().Be(ConsentProvenance.NotRecorded);
    }

    [Fact]
    public async Task AnUnreadableStoreRefusesEveryWriteInsteadOfReplacingIt()
    {
        // A read that failed used to seed the read-modify-write with an EMPTY store, so one unreadable
        // file made `existing` null: the merge that carries BlockedReason forward carried nulls —
        // silently clearing a persisted guard block — and the write republished a file containing only
        // the new entry, dropping every other game's record.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(Fingerprint(@"C:\a\other.exe")), ct);

        string path = Path.Combine(_dir, "games.json");
        string good = await File.ReadAllTextAsync(path, ct);
        await File.WriteAllTextAsync(path, "{ not json at all", ct);

        (await store.RecordOperatorAcknowledgementAsync(Ack(), ct)).Should().Be(ConsentWriteOutcome.Failed);
        (await store.RecordGuardBlockAsync(Fingerprint(), default, ct)).Should().Be(ConsentWriteOutcome.Failed);
        (await store.RevokeAsync(@"C:\a\other.exe", ct)).Should().Be(ConsentWriteOutcome.Failed);

        (await File.ReadAllTextAsync(path, ct)).Should().Be("{ not json at all",
            "a store we could not read must be left alone, not replaced with what we happen to hold");
        good.Should().Contain("other.exe", "and the record it held was real");
    }

    [Fact]
    public async Task AnUnreadableStoreListsNothingAndConsentsToNothing()
    {
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);
        await File.WriteAllTextAsync(Path.Combine(_dir, "games.json"), "{ not json", ct);

        (await store.FindAsync(Fingerprint().ExePath, ct)).IsFromStore.Should().BeFalse();
        (await store.ListEnabledAsync(ct)).Should().BeEmpty();
    }

    [Fact]
    public async Task ARegrantAgainstADifferentBinaryCannotInheritAnExistingBlock()
    {
        // ConsentWriteOutcome.StaleFingerprint documented this refusal and nothing produced it — a
        // declared-but-producerless value, which is the exact shape this PR invokes two files away to
        // justify ConsentProvenance having no FR-2.1 member.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);
        await store.RecordGuardBlockAsync(
            Fingerprint(),
            AntiCheatVerdict.Refused(AntiCheatRefusalReason.BlockedModule, "BattlEye", "BEClient_x64.dll"),
            ct);

        ExecutableFingerprint patched = Fingerprint() with { SizeBytes = 12345 };

        (await store.RecordOperatorAcknowledgementAsync(Ack(patched), ct))
            .Should().Be(ConsentWriteOutcome.StaleFingerprint);

        GameConsentRecord r = await store.FindAsync(Fingerprint().ExePath, ct);
        r.BlockedReason.Should().Contain("BattlEye");
        r.HookEnabled.Should().BeFalse();
    }

    [Fact]
    public async Task ARegrantAfterAPatchStillWorksWhenNothingIsBlocked()
    {
        // GREEN HALF. The refusal above must not make an ordinary re-consent after a game update
        // impossible — that is the normal way an operator re-acknowledges an updated title.
        CancellationToken ct = TestContext.Current.CancellationToken;
        FileGameConsentStore store = Store;
        await store.RecordOperatorAcknowledgementAsync(Ack(), ct);

        ExecutableFingerprint patched = Fingerprint() with { SizeBytes = 12345 };

        (await store.RecordOperatorAcknowledgementAsync(Ack(patched), ct)).Should().Be(ConsentWriteOutcome.Written);
        (await store.FindAsync(Fingerprint().ExePath, ct)).Fingerprint.SizeBytes.Should().Be(12345);
    }
}
