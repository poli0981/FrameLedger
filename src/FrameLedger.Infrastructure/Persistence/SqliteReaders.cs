using System.Data.Common;
using Dapper;
using Microsoft.Data.Sqlite;

namespace FrameLedger.Infrastructure.Persistence;

/// <summary>
/// Reader plumbing shared by the adapters: open, read, map with a plain (non-async) function, dispose.
/// Mapping stays synchronous on purpose — a row is already in memory once <c>ReadAsync</c> returned.
/// </summary>
internal static class SqliteReaders
{
    public static async Task<T?> ReadOneAsync<T>(SqliteConnection c, CommandDefinition command, Func<DbDataReader, T> map)
        where T : class
    {
        DbDataReader r = await c.ExecuteReaderAsync(command).ConfigureAwait(false);
        await using (r.ConfigureAwait(false))
        {
            return await r.ReadAsync(command.CancellationToken).ConfigureAwait(false) ? map(r) : null;
        }
    }

    public static async Task<IReadOnlyList<T>> ReadAllAsync<T>(SqliteConnection c, CommandDefinition command, Func<DbDataReader, T> map)
    {
        List<T> rows = [];
        DbDataReader r = await c.ExecuteReaderAsync(command).ConfigureAwait(false);
        await using (r.ConfigureAwait(false))
        {
            while (await r.ReadAsync(command.CancellationToken).ConfigureAwait(false))
            {
                rows.Add(map(r));
            }
        }

        return rows;
    }

    public static long? Int64(DbDataReader r, int ordinal) => r.IsDBNull(ordinal) ? null : r.GetInt64(ordinal);

    public static double? Double(DbDataReader r, int ordinal) => r.IsDBNull(ordinal) ? null : r.GetDouble(ordinal);

    public static string? String(DbDataReader r, int ordinal) => r.IsDBNull(ordinal) ? null : r.GetString(ordinal);

    /// <summary>
    /// A NULL column is <c>null</c>, never an empty memory — and the cast is load-bearing. Without it the
    /// conditional's natural type is <see cref="ReadOnlyMemory{T}"/>, because the null literal converts to it
    /// through the implicit <c>byte[]</c> operator as an EMPTY memory; the nullable wrap then reports
    /// <c>HasValue</c>, and "not measured" read as "an empty series was measured" until a test caught it.
    /// </summary>
    public static ReadOnlyMemory<byte>? Blob(DbDataReader r, int ordinal) =>
        r.IsDBNull(ordinal) ? (ReadOnlyMemory<byte>?)null : new ReadOnlyMemory<byte>((byte[])r.GetValue(ordinal));
}
