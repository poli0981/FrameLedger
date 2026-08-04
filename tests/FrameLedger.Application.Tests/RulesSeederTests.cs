using System.Security.Cryptography;
using FrameLedger.Application.Rules;

namespace FrameLedger.Application.Tests;

/// <summary>
/// §S20's decision table, one forced input per branch.
/// </summary>
/// <remarks>
/// The store is faked so every branch is reachable; the real filesystem and the
/// real guard parser are exercised by <c>FrameLedger.Infrastructure.Tests</c>.
/// What matters here is that no branch can be deleted without a test going red.
/// </remarks>
public sealed class RulesSeederTests
{
    private static byte[] Bytes(string s) => System.Text.Encoding.UTF8.GetBytes(s);

    private static string Hash(byte[] b) => Convert.ToHexString(SHA256.HashData(b));

    private sealed class FakeStore : IRulesStore
    {
        public byte[] Packaged { get; set; } = Bytes("packaged");
        public byte[]? Installed { get; set; }
        public string? Marker { get; set; }
        public HashSet<string> Unusable { get; } = [];
        public RulesWriteOutcome WriteResult { get; set; } = RulesWriteOutcome.Written;

        public byte[]? Written { get; private set; }
        public bool? WroteWithReplace { get; private set; }
        public int Writes { get; private set; }

        public ValueTask<byte[]?> ReadInstalledAsync(CancellationToken ct = default) => new(Installed);

        public ValueTask<byte[]> ReadPackagedSeedAsync(CancellationToken ct = default) => new(Packaged);

        public ValueTask<string?> ReadInstalledMarkerAsync(CancellationToken ct = default) => new(Marker);

        public bool IsUsableByTheGuard(byte[] candidate) =>
            !Unusable.Contains(System.Text.Encoding.UTF8.GetString(candidate), StringComparer.Ordinal);

        public ValueTask<RulesWriteOutcome> WriteAsync(byte[] content, bool replaceExisting,
            CancellationToken ct = default)
        {
            Writes++;
            Written = content;
            WroteWithReplace = replaceExisting;
            return new ValueTask<RulesWriteOutcome>(WriteResult);
        }
    }

    [Fact]
    public async Task NothingInstalledMeansInstallThePackagedSeed()
    {
        var store = new FakeStore();
        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.Installed, outcome);
        Assert.Equal(store.Packaged, store.Written);
        // Non-clobber on the absent path: if a file appeared meanwhile it is not
        // ours to overwrite.
        Assert.False(store.WroteWithReplace);
    }

    [Fact]
    public async Task AlreadyThePackagedSeedMeansDoNothing()
    {
        var store = new FakeStore { Installed = Bytes("packaged") };
        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.AlreadyCurrent, outcome);
        Assert.Equal(0, store.Writes);
    }

    [Fact]
    public async Task OurOwnStaleFileIsUpdated()
    {
        // Provenance, not version: the marker says we wrote what is there, and
        // this build ships something else.
        byte[] old = Bytes("what we installed last time");
        var store = new FakeStore { Installed = old, Marker = Hash(old) };

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.Updated, outcome);
        Assert.Equal(store.Packaged, store.Written);
        Assert.True(store.WroteWithReplace);
    }

    [Fact]
    public async Task AFileWeDidNotWriteIsLeftAlone()
    {
        // The safe act, and only because the floor is generated from the shipped
        // blocklist: a rules file can ADD to what the guard blocks and cannot
        // remove anything, so leaving a foreign file cannot weaken the gate.
        var store = new FakeStore { Installed = Bytes("somebody else's rules"), Marker = null };

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.ForeignLeftAlone, outcome);
        Assert.Equal(0, store.Writes);
    }

    [Fact]
    public async Task AStaleMarkerThatDoesNotMatchLeavesTheFileAlone()
    {
        // The marker names a different file from the one on disk, so we did not
        // write what is there. Treated as foreign, not as ours.
        var store = new FakeStore { Installed = Bytes("edited by hand"), Marker = Hash(Bytes("something else")) };

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.ForeignLeftAlone, outcome);
        Assert.Equal(0, store.Writes);
    }

    [Fact]
    public async Task AnUnusableInstalledFileIsReplacedWhoeverWroteIt()
    {
        // There is nothing to clobber — the guard already refuses every title on
        // this file — and nothing else in the product repairs it, so leaving it
        // would make a corrupt file a permanent machine-wide refusal.
        var store = new FakeStore { Installed = Bytes("truncated{"), Marker = null };
        store.Unusable.Add("truncated{");

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.ReplacedUnusable, outcome);
        Assert.True(store.WroteWithReplace);
    }

    [Fact]
    public async Task AnUnusablePackagedSeedInstallsNothing()
    {
        // Our own artifact is checked before it is trusted. CI gates it twice, so
        // this should be unreachable — which is exactly the sort of assumption
        // this project keeps finding to be false.
        var store = new FakeStore();
        store.Unusable.Add("packaged");

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.PackagedSeedUnusable, outcome);
        Assert.Equal(0, store.Writes);
    }

    [Fact]
    public async Task LosingTheRaceIsNotAFailure()
    {
        var store = new FakeStore { WriteResult = RulesWriteOutcome.AlreadyExists };

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.RaceLost, outcome);
    }

    [Fact]
    public async Task AFailedWriteIsReportedRatherThanSwallowed()
    {
        var store = new FakeStore { WriteResult = RulesWriteOutcome.Failed };

        RulesSeedOutcome outcome = await new RulesSeeder(store).EnsureSeededAsync(TestContext.Current.CancellationToken);

        Assert.Equal(RulesSeedOutcome.WriteFailed, outcome);
    }
}
