using System.Globalization;
using System.Reflection;
using Dapper;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// <c>06_DATA_MODEL</c> §Migrations: sequential embedded SQL, applied at startup by whichever process opens
/// the database first, guarded by <c>schema_migrations</c> and a named mutex. Never edits an applied
/// script; only appends.
/// </summary>
public static class MigrationRunner
{
    private const string _resourcePrefix = "FrameLedger.Infrastructure.Persistence.Migrations.";

    /// <summary>
    /// One per session (<c>Local\</c>), because the Agent and the UI run in the same session and the
    /// <c>Global\</c> namespace asks for a privilege a standard user need not hold.
    /// </summary>
    private const string _mutexName = @"Local\FrameLedger.Ledger.Migrate";

    /// <summary>The highest script version this build carries.</summary>
    public static int LatestVersion => Scripts().Keys.Max();

    /// <summary>Every embedded script, keyed by version, in order.</summary>
    public static SortedDictionary<int, string> Scripts()
    {
        var scripts = new SortedDictionary<int, string>();
        Assembly assembly = typeof(MigrationRunner).Assembly;
        foreach (string name in assembly.GetManifestResourceNames())
        {
            if (!name.StartsWith(_resourcePrefix, StringComparison.Ordinal) || !name.EndsWith(".sql", StringComparison.Ordinal))
            {
                continue;
            }

            // 0001_init.sql -> 1. The number is the version; the rest of the name is for humans.
            string file = name[_resourcePrefix.Length..];
            int underscore = file.IndexOf('_', StringComparison.Ordinal);
            int version = int.Parse(file[..underscore], NumberStyles.None, CultureInfo.InvariantCulture);
            using Stream stream = assembly.GetManifestResourceStream(name)!;
            using var reader = new StreamReader(stream);
            scripts.Add(version, reader.ReadToEnd());
        }

        return scripts;
    }

    /// <summary>Brings <paramref name="connection"/>'s schema to this build's version, or refuses.</summary>
    public static async Task<MigrationOutcome> ApplyAsync(SqliteConnection connection, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(connection);

        // The mutex serialises two processes opening the same file at once; the transaction below and
        // schema_migrations' primary key serialise the rest. Taken synchronously: this runs once, at open.
        using var mutex = new Mutex(initiallyOwned: false, _mutexName);
        bool held = false;
        try
        {
            try
            {
                held = mutex.WaitOne(TimeSpan.FromSeconds(30));
            }
            catch (AbandonedMutexException)
            {
                held = true;    // the previous holder died mid-migration; its transaction rolled back
            }

            if (!held)
            {
                throw new TimeoutException("another process has been migrating the ledger for over 30 s");
            }

            return await ApplyUnderMutexAsync(connection, ct).ConfigureAwait(false);
        }
        finally
        {
            if (held)
            {
                mutex.ReleaseMutex();
            }
        }
    }

    private static async Task<MigrationOutcome> ApplyUnderMutexAsync(SqliteConnection connection, CancellationToken ct)
    {
        // 0001_init.sql creates schema_migrations itself, so an empty file has no table to ask: version 0.
        bool hasTable = await connection.ExecuteScalarAsync<long>(new CommandDefinition(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'",
            cancellationToken: ct)).ConfigureAwait(false) > 0;
        long current = hasTable
            ? await connection.ExecuteScalarAsync<long?>(new CommandDefinition(
                "SELECT MAX(version) FROM schema_migrations", cancellationToken: ct)).ConfigureAwait(false) ?? 0
            : 0;

        SortedDictionary<int, string> scripts = Scripts();
        if (current > scripts.Keys.Max())
        {
            return MigrationOutcome.NewerThanThisBuild;
        }

        bool applied = false;
        foreach ((int version, string sql) in scripts)
        {
            if (version <= current)
            {
                continue;
            }

            // One transaction per script: a script that fails half-way leaves the version where it was,
            // and the next open runs it again from the top rather than from an unknown middle.
            var tx = (SqliteTransaction)await connection.BeginTransactionAsync(ct).ConfigureAwait(false);
            await using (tx.ConfigureAwait(false))
            {
                await connection.ExecuteAsync(new CommandDefinition(sql, transaction: tx, cancellationToken: ct)).ConfigureAwait(false);
                await connection.ExecuteAsync(new CommandDefinition(
                    "INSERT INTO schema_migrations (version, applied_at) VALUES (@version, @at)",
                    new { version, at = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() },
                    transaction: tx, cancellationToken: ct)).ConfigureAwait(false);
                await tx.CommitAsync(ct).ConfigureAwait(false);
            }

            applied = true;
        }

        return applied ? MigrationOutcome.Applied : MigrationOutcome.AlreadyCurrent;
    }
}
