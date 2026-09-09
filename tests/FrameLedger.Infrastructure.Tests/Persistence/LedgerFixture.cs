using FrameLedger.Infrastructure.Persistence;

namespace FrameLedger.Infrastructure.Tests.Persistence;

/// <summary>
/// A scratch ledger per test, never the real location. The real one is <c>%LOCALAPPDATA%\FrameLedger</c>,
/// the Agent's, and a test that opened it would leave rows on the machine that ran the suite.
/// </summary>
internal sealed class LedgerFixture : IAsyncDisposable
{
    private readonly string _dir = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "fl-ledger-" + Guid.NewGuid().ToString("N"));

    public string Path => System.IO.Path.Combine(_dir, LedgerPaths.DatabaseFileName);

    public LedgerDatabase Db { get; private set; } = null!;

    public static async Task<LedgerFixture> OpenAsync(int busyTimeoutMs = LedgerDatabase.DefaultBusyTimeoutMs)
    {
        var f = new LedgerFixture();
        f.Db = await LedgerDatabase.OpenAsync(f.Path, busyTimeoutMs, TestContext.Current.CancellationToken).ConfigureAwait(false);
        return f;
    }

    /// <summary>A second, independent connection to the same file — another process, as far as SQLite is concerned.</summary>
    public Task<LedgerDatabase> OpenAnotherAsync(CancellationToken ct, int busyTimeoutMs = LedgerDatabase.DefaultBusyTimeoutMs) =>
        LedgerDatabase.OpenAsync(Path, busyTimeoutMs, ct);

    public async ValueTask DisposeAsync()
    {
        await Db.DisposeAsync().ConfigureAwait(false);
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or DirectoryNotFoundException)
        {
        }
    }
}
