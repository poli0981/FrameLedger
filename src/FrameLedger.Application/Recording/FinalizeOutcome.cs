using System.Runtime.InteropServices;

namespace FrameLedger.Application.Recording;

/// <summary>What finalizing did.</summary>
[StructLayout(LayoutKind.Auto)]
public readonly record struct FinalizeOutcome(FinalizeStatus Status, long? SessionId, int RetentionSwept);
