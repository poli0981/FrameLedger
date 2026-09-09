namespace FrameLedger.Application.Recording;

public enum RecoveryStatus
{
    /// <summary>Finalized as <c>interrupted</c> from the file's valid prefix.</summary>
    Recovered = 0,

    /// <summary>Shorter than the minimum session length; the file is gone.</summary>
    Discarded,

    /// <summary>The finalize had landed before the process died; only the file was left. Gone now.</summary>
    AlreadyStored,

    /// <summary>No readable header: nothing to recover, and the file is gone.</summary>
    Unreadable,
}
