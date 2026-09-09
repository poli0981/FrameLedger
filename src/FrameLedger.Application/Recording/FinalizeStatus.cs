namespace FrameLedger.Application.Recording;

public enum FinalizeStatus
{
    /// <summary>Row, segments and blobs are in the ledger, in one transaction.</summary>
    Saved = 0,

    /// <summary>Shorter than the minimum session length: dropped, log only (<c>04_CAPTURE</c> §Discard rule).</summary>
    Discarded,

    /// <summary>A session with this guid was already stored — recovery ran after a finalize that did land.</summary>
    AlreadyStored,
}
