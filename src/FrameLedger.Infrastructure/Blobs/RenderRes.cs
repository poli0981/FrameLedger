using System.Runtime.InteropServices;

namespace FrameLedger.Infrastructure.Blobs;

/// <summary>One frame's two measured sizes.</summary>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RenderRes(ushort RenderW, ushort RenderH, ushort OutputW, ushort OutputH);
