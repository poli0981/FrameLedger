namespace FrameLedger.Application.Recording;

/// <summary>
/// The crash-safety artifact (<c>04_CAPTURE</c> §Ring draining, <c>06_DATA_MODEL</c> §The <c>.partial</c>
/// file): one append-only file per session under the Agent's <c>tmp\</c>, written every flush interval and
/// at every state transition, deleted by the finalize transaction's success, and read back by recovery on
/// the next start. Keyed by <c>session_guid</c>, like the row it becomes.
/// </summary>
public interface IPartialSessionStore
{
    /// <summary>Creates the file with its header. The writer owns the handle; disposing it does not delete the file.</summary>
    IPartialSessionWriter Create(PartialHeader header);

    /// <summary>Every session guid that has a file, oldest first — what recovery walks.</summary>
    IReadOnlyList<Guid> ListPending();

    /// <summary>The valid prefix of a file: every chunk up to the first one that is short or fails its check.</summary>
    PartialSession? Read(Guid sessionGuid);

    /// <summary>Removes the file; a missing file is not an error.</summary>
    void Delete(Guid sessionGuid);
}
