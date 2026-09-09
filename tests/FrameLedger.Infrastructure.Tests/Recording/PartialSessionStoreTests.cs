using FluentAssertions;
using FrameLedger.Application.Recording;
using FrameLedger.Domain.Sessions;
using FrameLedger.Infrastructure.Recording;

namespace FrameLedger.Infrastructure.Tests.Recording;

public sealed class PartialSessionStoreTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "fl-pstore-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, recursive: true);
        }
        catch (IOException)
        {
        }
    }

    private static PartialHeader Header(Guid guid) => new()
    {
        SessionGuid = guid,
        StartedAt = DateTimeOffset.UnixEpoch,
        QpcEpoch = 1,
        QpcFrequency = 10_000_000,
        GameId = 1,
        SnapshotId = 1,
        ExePath = @"C:\g\game.exe",
        Tier = CaptureTier.NotHooked,
        Mode = CaptureMode.Attach,
    };

    [Fact]
    public void CreatesUnderTheDirectoryListsOldestFirstReadsBackAndDeletes()
    {
        var store = new PartialSessionStore(Path.Combine(_dir, "tmp"));
        var a = Guid.NewGuid();
        var b = Guid.NewGuid();

        store.ListPending().Should().BeEmpty("no directory yet is no sessions, not an error");
        store.Create(Header(a)).Dispose();
        Thread.Sleep(20);
        store.Create(Header(b)).Dispose();
        File.WriteAllText(Path.Combine(store.Directory, "not-a-guid.partial"), "x");

        store.ListPending().Should().Equal(a, b);
        store.Read(a)!.Header.SessionGuid.Should().Be(a);
        store.Read(Guid.NewGuid()).Should().BeNull();

        store.Delete(a);
        store.Delete(a);
        store.ListPending().Should().Equal(b);
    }
}
