using Dapper;
using FluentAssertions;
using FrameLedger.Infrastructure.Persistence;

namespace FrameLedger.Infrastructure.Tests.Persistence;

/// <summary>
/// <c>14_TESTING</c>: "SQLite migrations apply cleanly from an empty file to the current schema, and re-applying
/// is a no-op" — plus the pragmas, the refusal of a newer schema, and the transaction guarantee.
/// </summary>
public sealed class LedgerDatabaseTests
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    private static readonly string[] _tables =
    [
        "schema_migrations", "games", "hardware_snapshots", "sessions", "session_segments", "frame_blobs",
        "sensor_blobs", "session_annotations", "settings", "legal_acceptance",
    ];

    [Fact]
    public async Task AnEmptyFileMigratesToTheCurrentSchema()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();

        f.Db.Migration.Should().Be(MigrationOutcome.Applied);
        f.Db.SchemaVersion.Should().Be(MigrationRunner.LatestVersion);
        IReadOnlyList<string> tables = await f.Db.ReadAsync(async (c, ct) =>
            (IReadOnlyList<string>)[.. await c.QueryAsync<string>(new CommandDefinition(
                "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name", cancellationToken: ct)).ConfigureAwait(false)], Ct);
        tables.Should().Contain(_tables);
    }

    [Fact]
    public async Task ReopeningIsANoOp()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        await using LedgerDatabase again = await f.OpenAnotherAsync(Ct);

        again.Migration.Should().Be(MigrationOutcome.AlreadyCurrent);
        again.SchemaVersion.Should().Be(f.Db.SchemaVersion);
        long rows = await again.ReadAsync((c, ct) => c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT COUNT(*) FROM schema_migrations", cancellationToken: ct)), Ct);
        rows.Should().Be(MigrationRunner.LatestVersion, "one row per applied script, and nothing re-applied");
    }

    [Fact]
    public async Task ThePragmasAreTheDocumentedOnes()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync(busyTimeoutMs: 1234);

        (string journal, long sync, long fk, long busy) = await f.Db.ReadAsync(async (c, ct) => (
            (await c.ExecuteScalarAsync<string?>(new CommandDefinition("PRAGMA journal_mode", cancellationToken: ct)).ConfigureAwait(false))!,
            await c.ExecuteScalarAsync<long>(new CommandDefinition("PRAGMA synchronous", cancellationToken: ct)).ConfigureAwait(false),
            await c.ExecuteScalarAsync<long>(new CommandDefinition("PRAGMA foreign_keys", cancellationToken: ct)).ConfigureAwait(false),
            await c.ExecuteScalarAsync<long>(new CommandDefinition("PRAGMA busy_timeout", cancellationToken: ct)).ConfigureAwait(false)), Ct);

        journal.Should().Be("wal");
        sync.Should().Be(1, "NORMAL");
        fk.Should().Be(1);
        busy.Should().Be(1234);
    }

    [Fact]
    public async Task ASchemaNewerThanThisBuildIsRefusedNotGuessedAt()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        await f.Db.WriteAsync((c, tx, ct) => c.ExecuteAsync(new CommandDefinition(
            "INSERT INTO schema_migrations (version, applied_at) VALUES (999, 0)", transaction: tx, cancellationToken: ct)), Ct);

        Func<Task> open = async () => await f.OpenAnotherAsync(Ct).ConfigureAwait(false);

        (await open.Should().ThrowAsync<LedgerSchemaException>().ConfigureAwait(true)).Which.Message.Should().Contain("999");
    }

    [Fact]
    public async Task AWriteThatThrowsIsRolledBackWhole()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();

        Func<Task> write = async () => await f.Db.WriteAsync<int>(async (c, tx, ct) =>
        {
            await c.ExecuteAsync(new CommandDefinition("INSERT INTO settings (key, value) VALUES ('a', '1')", transaction: tx, cancellationToken: ct)).ConfigureAwait(false);
            throw new InvalidOperationException("half-way");
        }, Ct).ConfigureAwait(false);

        await write.Should().ThrowAsync<InvalidOperationException>();
        long rows = await f.Db.ReadAsync((c, ct) => c.ExecuteScalarAsync<long>(new CommandDefinition("SELECT COUNT(*) FROM settings", cancellationToken: ct)), Ct);
        rows.Should().Be(0, "the transaction rolled back, so the first statement went with the second");
    }

    [Fact]
    public async Task TwoConnectionsSerialiseThroughBusyTimeoutRatherThanFailing()
    {
        await using LedgerFixture f = await LedgerFixture.OpenAsync();
        await using LedgerDatabase other = await f.OpenAnotherAsync(Ct);

        await f.Db.WriteAsync((c, tx, ct) => c.ExecuteAsync(new CommandDefinition("INSERT INTO settings (key, value) VALUES ('k', 'one')", transaction: tx, cancellationToken: ct)), Ct);
        await other.WriteAsync((c, tx, ct) => c.ExecuteAsync(new CommandDefinition("UPDATE settings SET value = 'two' WHERE key = 'k'", transaction: tx, cancellationToken: ct)), Ct);

        string? value = await f.Db.ReadAsync((c, ct) => c.ExecuteScalarAsync<string?>(new CommandDefinition("SELECT value FROM settings WHERE key = 'k'", cancellationToken: ct)), Ct);
        value.Should().Be("two", "WAL lets a second opener write once the first's transaction has committed");
    }

    [Fact]
    public void TheDefaultLocationIsTheAgentsDirectory()
    {
        LedgerPaths.DefaultDatabase.Should().StartWith(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
        LedgerPaths.DefaultDatabase.Should().EndWith(@"FrameLedger\ledger.db");
    }

    [Fact]
    public void TheScriptsAreNumberedFromOne()
    {
        SortedDictionary<int, string> scripts = MigrationRunner.Scripts();

        scripts.Keys.Should().StartWith(1);
        scripts.Keys.Should().BeInAscendingOrder().And.OnlyHaveUniqueItems();
        scripts[1].Should().Contain("CREATE TABLE games");
    }
}
