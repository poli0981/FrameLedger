using System.Runtime.InteropServices;

namespace FrameLedger.Application.Recording;

/// <summary>What recovery did with one <c>.partial</c>.</summary>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RecoveryOutcome(Guid SessionGuid, RecoveryStatus Status, long? SessionId, string Detail);
