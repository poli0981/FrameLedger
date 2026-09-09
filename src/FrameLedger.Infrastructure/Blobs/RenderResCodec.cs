namespace FrameLedger.Infrastructure.Blobs;

/// <summary>
/// <c>frame_blobs.render_res</c>: TWO pairs per frame (render W/H, output W/H) as one <c>uint16[]</c>,
/// stored only when either varies across the session.
/// </summary>
public static class RenderResCodec
{
    public static byte[] Encode(IReadOnlyList<RenderRes> frames)
    {
        ArgumentNullException.ThrowIfNull(frames);

        var flat = new ushort[frames.Count * 4];
        for (int i = 0; i < frames.Count; i++)
        {
            flat[i * 4] = frames[i].RenderW;
            flat[(i * 4) + 1] = frames[i].RenderH;
            flat[(i * 4) + 2] = frames[i].OutputW;
            flat[(i * 4) + 3] = frames[i].OutputH;
        }

        return SeriesCodec.EncodeUInt16(flat);
    }

    public static RenderRes[] Decode(byte[] blob)
    {
        ushort[] flat = SeriesCodec.DecodeUInt16(blob);
        if (flat.Length % 4 != 0)
        {
            throw new InvalidDataException("a render_res blob must hold two pairs per frame");
        }

        var frames = new RenderRes[flat.Length / 4];
        for (int i = 0; i < frames.Length; i++)
        {
            frames[i] = new RenderRes(flat[i * 4], flat[(i * 4) + 1], flat[(i * 4) + 2], flat[(i * 4) + 3]);
        }

        return frames;
    }

    /// <summary>True when every frame ran at the same two sizes — the column may then stay NULL.</summary>
    public static bool IsConstant(IReadOnlyList<RenderRes> frames)
    {
        ArgumentNullException.ThrowIfNull(frames);
        return frames.Count == 0 || frames.All(f => f == frames[0]);
    }
}
