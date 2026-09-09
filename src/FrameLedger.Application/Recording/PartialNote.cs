using System.Runtime.InteropServices;

namespace FrameLedger.Application.Recording;

/// <summary>A state transition, as the recorder wrote it — the breadcrumb <c>19_SAFETY</c> §Crash safety asks for.</summary>
[StructLayout(LayoutKind.Auto)]
public readonly record struct PartialNote(DateTimeOffset At, string Text);
