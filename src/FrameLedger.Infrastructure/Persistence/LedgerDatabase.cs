using Dapper;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// The one open connection to <c>ledger.db</c> in this process, with <c>06_DATA_MODEL</c>'s pragmas set
/// and its schema migrated before anyone reads it.
/// </summary>
/// <remarks>
/// <para>
/// <b>One connection, one gate.</b> <c>04_CAPTURE</c> §Threading model: the session loop finalizes on its
/// own task, the console verbs run on the main one, and SQLite serialises writers anyway — so a single
/// connection behind a <see cref="SemaphoreSlim"/> is the honest shape, and every write is an explicit
/// transaction (<see cref="WriteAsync{T}"/>) or does not happen. WAL lets another process (the UI, P3)
/// read while this one writes.
/// </para>
/// <para>
/// <b>Opening refuses a newer schema.</b> A ledger written by a build newer than this one is not read on
/// a guess; <see cref="OpenAsync"/> throws <see cref="LedgerSchemaException"/> and the caller tells the
/// user to update.
/// </para>
/// </remarks>
public sealed class LedgerDatabase : IAsyncDisposable
{
    /// <summary><c>06_DATA_MODEL</c>: <c>busy_timeout=5000</c>.</summary>
    public const int DefaultBusyTimeoutMs = 5000;

    private readonly SqliteConnection _connection;
    private readonly SemaphoreSlim _gate = new(1, 1);

    private LedgerDatabase(string path, SqliteConnection connection, MigrationOutcome migration, long schemaVersion)
    {
        Path = path;
        _connection = connection;
        Migration = migration;
        SchemaVersion = schemaVersion;
    }

    /// <summary>The file this instance opened.</summary>
    public string Path { get; }

    /// <summary>What opening did to the schema.</summary>
    public MigrationOutcome Migration { get; }

    /// <summary>The schema version after opening.</summary>
    public long SchemaVersion { get; }

    /// <summary>
    /// Opens (creating the file and its directory if needed), sets the pragmas, migrates. A path is
    /// REQUIRED: <see cref="LedgerPaths.DefaultDatabase"/> is the Agent's; anything else says why.
    /// </summary>
    public static async Task<LedgerDatabase> OpenAsync(string path, int busyTimeoutMs = DefaultBusyTimeoutMs,
        CancellationToken ct = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentOutOfRangeException.ThrowIfNegative(busyTimeoutMs);

        string full = System.IO.Path.GetFullPath(path);
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(full)!);

        var connection = new SqliteConnection(new SqliteConnectionStringBuilder
        {
            DataSource = full,
            Mode = SqliteOpenMode.ReadWriteCreate,
            Pooling = false,
        }.ToString());

        try
        {
            await connection.OpenAsync(ct).ConfigureAwait(false);

            // 06_DATA_MODEL's four pragmas. busy_timeout first: WAL's own journal switch can contend.
            await connection.ExecuteAsync(new CommandDefinition("PRAGMA busy_timeout = " + busyTimeoutMs.ToString(System.Globalization.CultureInfo.InvariantCulture), cancellationToken: ct)).ConfigureAwait(false);
            await connection.ExecuteAsync(new CommandDefinition("PRAGMA journal_mode = WAL", cancellationToken: ct)).ConfigureAwait(false);
            await connection.ExecuteAsync(new CommandDefinition("PRAGMA synchronous = NORMAL", cancellationToken: ct)).ConfigureAwait(false);
            await connection.ExecuteAsync(new CommandDefinition("PRAGMA foreign_keys = ON", cancellationToken: ct)).ConfigureAwait(false);

            MigrationOutcome migration = await MigrationRunner.ApplyAsync(connection, ct).ConfigureAwait(false);
            long version = await connection.ExecuteScalarAsync<long?>(new CommandDefinition(
                "SELECT MAX(version) FROM schema_migrations", cancellationToken: ct)).ConfigureAwait(false) ?? 0;
            if (migration == MigrationOutcome.NewerThanThisBuild)
            {
                throw new LedgerSchemaException(
                    $"{full} is at schema version {version}, newer than this build's {MigrationRunner.LatestVersion}; refusing to read it");
            }

            return new LedgerDatabase(full, connection, migration, version);
        }
        catch
        {
            await connection.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <summary>Runs <paramref name="query"/> on the connection, serialised with every other caller.</summary>
    public async ValueTask<T> ReadAsync<T>(Func<SqliteConnection, CancellationToken, Task<T>> query, CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(query);

        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            return await query(_connection, ct).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>
    /// Runs <paramref name="work"/> inside one explicit transaction: committed when it returns, rolled back
    /// when it throws. <c>06_DATA_MODEL</c>: "all writes in explicit transactions".
    /// </summary>
    public async ValueTask<T> WriteAsync<T>(Func<SqliteConnection, SqliteTransaction, CancellationToken, Task<T>> work,
        CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(work);

        await _gate.WaitAsync(ct).ConfigureAwait(false);
        try
        {
            var tx = (SqliteTransaction)await _connection.BeginTransactionAsync(ct).ConfigureAwait(false);
            await using (tx.ConfigureAwait(false))
            {
                try
                {
                    T result = await work(_connection, tx, ct).ConfigureAwait(false);
                    await tx.CommitAsync(ct).ConfigureAwait(false);
                    return result;
                }
                catch
                {
                    await tx.RollbackAsync(ct).ConfigureAwait(false);
                    throw;
                }
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        await _connection.DisposeAsync().ConfigureAwait(false);
        _gate.Dispose();
    }
}
