using System.Globalization;
using FrameLedger.Application.Recording;

namespace FrameLedger.Infrastructure.Recording;

/// <summary>
/// <c>&lt;directory&gt;\&lt;sessionGuid&gt;.partial</c>, one per session — the Agent's <c>tmp\</c> under
/// <c>%LOCALAPPDATA%\FrameLedger</c>, the unshipped host's <c>tmp\</c> beside its binary (decision D5).
/// </summary>
public sealed class PartialSessionStore : IPartialSessionStore
{
    private const string _extension = ".partial";

    public PartialSessionStore(string directory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(directory);
        Directory = directory;
    }

    public string Directory { get; }

    public IPartialSessionWriter Create(PartialHeader header)
    {
        ArgumentNullException.ThrowIfNull(header);
        System.IO.Directory.CreateDirectory(Directory);
        return PartialSessionFile.Create(PathOf(header.SessionGuid), header);
    }

    public IReadOnlyList<Guid> ListPending()
    {
        if (!System.IO.Directory.Exists(Directory))
        {
            return [];
        }

        return System.IO.Directory.EnumerateFiles(Directory, "*" + _extension)
            .Select(static f => (Path: f, Guid: Guid.TryParseExact(Path.GetFileNameWithoutExtension(f), "N", out Guid g) ? g : (Guid?)null))
            .Where(static x => x.Guid is not null)
            .OrderBy(static x => File.GetCreationTimeUtc(x.Path))
            .Select(static x => x.Guid!.Value)
            .ToArray();
    }

    public PartialSession? Read(Guid sessionGuid)
    {
        string path = PathOf(sessionGuid);
        return File.Exists(path) ? PartialSessionFile.Read(path) : null;
    }

    public void Delete(Guid sessionGuid) => File.Delete(PathOf(sessionGuid));

    private string PathOf(Guid guid) => Path.Combine(Directory, guid.ToString("N", CultureInfo.InvariantCulture) + _extension);
}
