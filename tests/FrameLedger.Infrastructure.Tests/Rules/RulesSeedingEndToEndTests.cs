using System.Security.Cryptography;
using FrameLedger.Application.Rules;
using FrameLedger.Infrastructure.AntiCheat;
using FrameLedger.Infrastructure.Rules;

namespace FrameLedger.Infrastructure.Tests.Rules;

/// <summary>
/// §S20 against the real filesystem and the real guard parser.
/// </summary>
public sealed class RulesSeedingEndToEndTests
{
    private static string PackagedSeed() => Path.Combine(AppContext.BaseDirectory, "rules", "detection-rules.json");

    [Fact]
    public void ThePackagedSeedShipsAndIsTheFileCiValidated()
    {
        // Without this, the seeder and its own tests could agree with each other
        // while the artifact carried nothing — PreserveNewest compares timestamps,
        // so a stale copy is a real outcome rather than a hypothetical one.
        Assert.True(File.Exists(PackagedSeed()), $"no packaged seed at {PackagedSeed()}");

        string repoRoot = RepoRoot();
        string inRepo = Path.Combine(repoRoot, "rules", "detection-rules.json");
        Assert.True(File.Exists(inRepo), $"no repository seed at {inRepo}");

        Assert.Equal(
            Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(inRepo))),
            Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(PackagedSeed()))));
    }

    [Fact]
    public void TheGuardsOwnParserAcceptsThePackagedSeedAndRejectsRubbish()
    {
        // Both directions, because a validator that accepts everything would pass
        // the first assertion on its own.
        Assert.Equal(0, NativeAntiCheatGuard.NativeCheckRules(File.ReadAllBytes(PackagedSeed())));
        Assert.NotEqual(0, NativeAntiCheatGuard.NativeCheckRules("{ not json"u8.ToArray()));
        Assert.NotEqual(0, NativeAntiCheatGuard.NativeCheckRules("""{"anticheat":{"modules":[]}}"""u8.ToArray()));
    }

    [Fact]
    public async Task SeedingIntoAnEmptyDirectoryProducesAFileTheGuardAccepts()
    {
        // A scratch destination, NOT the machine's real rules location: a test
        // that mutates the product's one file leaves the guard refusing every
        // title if it dies between delete and restore.
        string dir = Path.Combine(Path.GetTempPath(), "fl-seed-" + Guid.NewGuid().ToString("N")[..8]);
        string dest = Path.Combine(dir, "rules", "detection-rules.json");
        try
        {
            var store = new FileSystemRulesStore(dest, PackagedSeed());
            var seeder = new RulesSeeder(store);

            Assert.Equal(RulesSeedOutcome.Installed, await seeder.EnsureSeededAsync(TestContext.Current.CancellationToken));
            Assert.True(File.Exists(dest));
            Assert.Equal(0, NativeAntiCheatGuard.NativeCheckRules(await File.ReadAllBytesAsync(dest, TestContext.Current.CancellationToken)));

            // Idempotent: a second run must not rewrite a current file.
            Assert.Equal(RulesSeedOutcome.AlreadyCurrent, await seeder.EnsureSeededAsync(TestContext.Current.CancellationToken));

            // Our own stale file is updated, and the backup ReplaceFileW keeps is
            // what makes 05_DETECTION's "the last valid copy is kept" a mechanism.
            await File.WriteAllTextAsync(dest, "{ deliberately unusable", TestContext.Current.CancellationToken);
            Assert.Equal(RulesSeedOutcome.ReplacedUnusable, await seeder.EnsureSeededAsync(TestContext.Current.CancellationToken));
            Assert.Equal(0, NativeAntiCheatGuard.NativeCheckRules(await File.ReadAllBytesAsync(dest, TestContext.Current.CancellationToken)));
            Assert.True(File.Exists(dest + ".bak"), "the replaced file should have been kept");
        }
        finally
        {
            if (Directory.Exists(dir))
            {
                Directory.Delete(dir, recursive: true);
            }
        }
    }

    [Fact]
    public async Task AUsableFileWeDidNotWriteIsLeftAlone()
    {
        string dir = Path.Combine(Path.GetTempPath(), "fl-seed-" + Guid.NewGuid().ToString("N")[..8]);
        string dest = Path.Combine(dir, "rules", "detection-rules.json");
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            // A real, usable rules document that we did not install: the shipped
            // seed with one extra byte of whitespace, so its hash differs.
            byte[] foreign = [.. await File.ReadAllBytesAsync(PackagedSeed(), TestContext.Current.CancellationToken), (byte)'\n'];
            await File.WriteAllBytesAsync(dest, foreign, TestContext.Current.CancellationToken);

            var seeder = new RulesSeeder(new FileSystemRulesStore(dest, PackagedSeed()));
            Assert.Equal(RulesSeedOutcome.ForeignLeftAlone, await seeder.EnsureSeededAsync(TestContext.Current.CancellationToken));
            Assert.Equal(foreign, await File.ReadAllBytesAsync(dest, TestContext.Current.CancellationToken));
        }
        finally
        {
            if (Directory.Exists(dir))
            {
                Directory.Delete(dir, recursive: true);
            }
        }
    }

    [Fact]
    public void TheStoreWritesWhereTheGUARDReads()
    {
        // The parameterless store resolves its destination from the guard's own
        // exported path, so a seeder that writes where the gate does not read is
        // impossible by construction rather than by assertion.
        Assert.Equal(NativeAntiCheatGuard.NativeRulesFilePath(), new FileSystemRulesStore().Destination);
    }

    private static string RepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "FrameLedger.slnx")))
        {
            dir = dir.Parent;
        }
        Assert.NotNull(dir);
        return dir!.FullName;
    }
}
