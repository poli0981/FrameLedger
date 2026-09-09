namespace FrameLedger.Domain.Metrics;

/// <summary>Mirror of <c>FlFgMode</c>: which frame-generation technology a sample named.</summary>
public enum FgKind
{
    NotReported = 0,
    DlssG = 1,
    FsrFg = 2,
    XeFg = 3,

    /// <summary>A hook ran and there was no frame generation.</summary>
    None = 4,

    /// <summary>A hook ran and could not identify what it saw. Different from "not measured".</summary>
    Unknown = 0xFF,
}
