namespace FrameLedger.Shared;

/// <summary>
/// Why a capture side may not be attached to. <c>Ok</c> is the ONLY value that permits attaching, and
/// it is deliberately not the default: a zeroed result has validated nothing and must not read as
/// permission — the same rule <c>AntiCheatVerdict</c> follows for the guard.
/// </summary>
public enum ShmAttachRefusal
{
    /// <summary>Nobody validated anything. Never returned by <see cref="ShmHandshakeValidator"/>.</summary>
    NotEvaluated = 0,

    Ok = 1,

    /// <summary>The Overlay in that process writes a layout this build does not understand.</summary>
    LayoutVersionMismatch = 2,

    /// <summary>Same layout version, different record size — belt-and-braces against struct drift.</summary>
    RecordSizeMismatch = 3,

    /// <summary>
    /// The app was updated while the game was running, so the DLL inside it is from another build.
    /// <c>04_CAPTURE</c>: tell the user to restart the game.
    /// </summary>
    BuildIdMismatch = 4,

    /// <summary>Capacity is not a power of two, so the ring's index masking is meaningless.</summary>
    CapacityInvalid = 5,

    /// <summary>
    /// The capacity is a legal power of two, but the ring it describes does not fit in the mapping we
    /// were given. Indexing it would read past the end of the section.
    /// </summary>
    CapacityExceedsMapping = 7,

    /// <summary>
    /// The handshake is not published yet, or we have no build id of our own to compare against.
    /// "Could not look" is its own answer and must never collapse into <see cref="Ok"/>.
    /// </summary>
    Incomplete = 6,
}

/// <summary>
/// The refuse-to-attach check <c>07_IPC</c> §Protocol rules and <c>04_CAPTURE</c> §Ring draining both
/// specify, as a pure function so it can be driven in every direction without a live target.
/// <para>
/// It existed as prose in two documents and as code nowhere, because the input it needs — the Agent's
/// own build id — had no source (<c>20_OPEN_QUESTIONS</c> §S23-1).
/// </para>
/// </summary>
public static class ShmHandshakeValidator
{
    /// <summary>
    /// Decides whether a published handshake may be attached to.
    /// </summary>
    /// <param name="handshake">The mapped region 1, read once.</param>
    /// <param name="ownBuildId">This install's build id, from <c>FlGuardBuildId</c>. Empty means we could not obtain it, which refuses.</param>
    /// <param name="mappedBytes">
    /// The size of the view we actually hold, from <c>SafeMemoryMappedViewHandle.ByteLength</c> — never
    /// <see cref="ShmLayout.DefaultCapacity"/>, which would be checking the handshake against an
    /// assumption instead of against the mapping.
    /// </param>
    public static ShmAttachRefusal Validate(FlShmHandshake handshake, string? ownBuildId, long mappedBytes)
    {
        // ORDER MATTERS, and it is version first for a reason: every field below is only meaningful
        // under a layout we agree on. Reporting "record size wrong" about a struct laid out by a
        // version we do not know would name the wrong cause, and the user-facing text differs.
        if (handshake.LayoutVersion == 0)
        {
            // The Overlay publishes layoutVersion LAST, behind a release fence, precisely so this
            // reads as "not ready yet" rather than as a ring of zero slots.
            return ShmAttachRefusal.Incomplete;
        }

        if (handshake.LayoutVersion != ShmLayout.LayoutVersion)
        {
            return ShmAttachRefusal.LayoutVersionMismatch;
        }

        if (handshake.RecordSize != 64)
        {
            return ShmAttachRefusal.RecordSizeMismatch;
        }

        if (handshake.Capacity == 0 || (handshake.Capacity & (handshake.Capacity - 1)) != 0)
        {
            return ShmAttachRefusal.CapacityInvalid;
        }

        // AND IT MUST FIT. A power of two is not by itself a safe capacity: every value up to 2^31 is
        // one, and the reader indexes the ring by raw pointer arithmetic because the seqlock needs
        // ordering that MemoryMappedViewAccessor.Read<T> does not provide — so it gives up that API's
        // bounds check at the same time. Nothing else in the drain relates the handshake's claim to the
        // section we were actually given.
        //
        // No benign divergence exists today: the writer sizes the section and publishes the capacity
        // from one constant, and the buildId gate above already refuses any writer that is not our own
        // build. This is hardening for a region that is mapped read-write inside a game process we do
        // not control, and for a future writer that sizes the ring at runtime.
        if (ShmLayout.SizeForCapacity(handshake.Capacity) > mappedBytes)
        {
            return ShmAttachRefusal.CapacityExceedsMapping;
        }

        if (string.IsNullOrEmpty(ownBuildId))
        {
            // We could not obtain our own id. This is the state that made the whole check
            // unimplementable, and it must refuse: comparing "" with "" would have "matched" forever.
            return ShmAttachRefusal.Incomplete;
        }

        string theirs = handshake.BuildIdString();
        if (theirs.Length == 0)
        {
            return ShmAttachRefusal.Incomplete;
        }

        return string.Equals(theirs, ownBuildId, StringComparison.Ordinal)
            ? ShmAttachRefusal.Ok
            : ShmAttachRefusal.BuildIdMismatch;
    }
}
