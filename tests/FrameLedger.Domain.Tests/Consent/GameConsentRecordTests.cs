using System.Reflection;
using FluentAssertions;
using FrameLedger.Domain.Consent;

namespace FrameLedger.Domain.Tests.Consent;

public sealed class GameConsentRecordTests
{
    private static ExecutableFingerprint Fingerprint(string path = @"C:\Games\Title\game.exe") =>
        new() { ExePath = path, SizeBytes = 1234, MtimeUnixMs = 1_700_000_000_000 };

    [Fact]
    public void ADefaultRecordHasNotComeFromAnyStoreAndConsentsToNothing()
    {
        GameConsentRecord none = default;

        none.IsFromStore.Should().BeFalse("a record nobody produced has recorded nothing");
        none.HookEnabled.Should().BeFalse();
        none.ConsentedAt.Should().BeNull();
        none.Provenance.Should().Be(ConsentProvenance.NotRecorded);
        none.PreScanUnverified.Should().BeFalse();
    }

    [Fact]
    public void TheRecordHasNoPublicWayToSetTheGatesInputs()
    {
        // WHAT THIS REPLACES, and why the replacement is not cosmetic. The first shape of this type used
        // `init` accessors behind a private constructor, and asserted confinement on the constructor.
        // That confines nothing: a struct always has an accessible parameterless constructor and an
        // object initialiser reaches every `init` member, so
        //     default(GameConsentRecord) with { HookEnabled = true, ConsentedAt = DateTimeOffset.UtcNow }
        // compiles from anywhere — §S27's rejected synthesis with two extra words.
        //
        // AntiCheatVerdict is the precedent: get-only properties plus factories, which is why
        // `with { Reason = ... }` does not compile against IT either.
        IEnumerable<string> settable = typeof(GameConsentRecord)
            .GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.SetMethod is { IsPublic: true })
            .Select(p => p.Name);

        settable.Should().BeEmpty(
            "get-only properties are what make the synthesis inexpressible rather than merely discouraged");
    }

    [Fact]
    public void NothingInTheShippedClosureCanMintARecord()
    {
        // FrameLedger.App and FrameLedger.Agent both reach this assembly through FrameLedger.Application,
        // and 12_BUILD publishes both roots into one out/app. A public Stored() would therefore be a
        // blessed, SHIPPED consent-minting API — and tools/package-closure-check.ps1 cannot see it,
        // because it walks project references and Domain is legitimately inside both closures.
        //
        // This Fact is what makes widening it a deliberate act. The InternalsVisibleTo list in
        // FrameLedger.Domain.csproj is the other half, and it is reviewable in a diff.
        typeof(GameConsentRecord)
            .GetMethod("Stored", BindingFlags.Public | BindingFlags.Static)
            .Should().BeNull("the minting factory is internal; see FrameLedger.Domain.csproj for who may reach it");

        typeof(GameConsentRecord)
            .GetMethod("Stored", BindingFlags.NonPublic | BindingFlags.Static)
            .Should().NotBeNull("...and it must still exist, or this assertion is about a typo");
    }

    [Fact]
    public void OnlyTheStoredFactoryProducesAStoredRecord()
    {
        GameConsentRecord record = GameConsentRecord.Stored(
            Fingerprint(), hookEnabled: true, consentedAt: DateTimeOffset.UnixEpoch,
            ConsentProvenance.UnshippedHostOperator, "unshipped-host-operator/1", blockedReason: null,
            preScanUnverified: false, updatedAt: DateTimeOffset.UnixEpoch);

        record.IsFromStore.Should().BeTrue();
        record.DisclosureVersion.Should().Be("unshipped-host-operator/1");
        record.HookEnabled.Should().BeTrue();
    }

    [Fact]
    public void AMissingDisclosureVersionBecomesEmptyRatherThanNull()
    {
        GameConsentRecord record = GameConsentRecord.Stored(
            Fingerprint(), hookEnabled: false, consentedAt: null, ConsentProvenance.NotRecorded,
            disclosureVersion: null, blockedReason: null, preScanUnverified: false,
            updatedAt: DateTimeOffset.UnixEpoch);

        record.DisclosureVersion.Should().BeEmpty();
    }

    [Fact]
    public void NoProvenanceValueClaimsTheConsentDialogThatDoesNotExist()
    {
        // FR-2.1's dialog needs reviewed Safety_* wording in en/vi/ja, and no .resx file exists anywhere
        // in this tree. A declared-but-producerless value is the "reads as sanctioned" shape §S29(c) was
        // raised for, so adding one has to be a deliberate act that turns this red.
        string[] names = Enum.GetNames<ConsentProvenance>();

        names.Should().BeEquivalentTo(
            [nameof(ConsentProvenance.NotRecorded), nameof(ConsentProvenance.UnshippedHostOperator)]);
        ((int)ConsentProvenance.NotRecorded).Should().Be(0, "the default must mean no disclosure was shown");
    }

    [Fact]
    public void AFingerprintMatchesOnlyTheSameExecutable()
    {
        ExecutableFingerprint stored = Fingerprint();

        stored.Matches(stored).Should().BeTrue();
        stored.Matches(stored with { SizeBytes = 1235 }).Should().BeFalse("a patched executable is a different one");
        stored.Matches(stored with { MtimeUnixMs = 1 }).Should().BeFalse();
        stored.Matches(stored with { ExePath = @"C:\Elsewhere\game.exe" }).Should().BeFalse(
            "04_CAPTURE's filename fallback is for the watchlist; applied to consent it lets a different "
            + "binary inherit an existing record, which is the widening polarity");
    }

    [Fact]
    public void PathComparisonIsCaseInsensitiveBecauseWindowsPathsAre()
    {
        Fingerprint().Matches(Fingerprint(@"c:\games\title\GAME.EXE")).Should().BeTrue();
    }
}
