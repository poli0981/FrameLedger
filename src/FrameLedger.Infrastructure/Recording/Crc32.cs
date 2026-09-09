namespace FrameLedger.Infrastructure.Recording;

/// <summary>CRC-32 (IEEE 802.3, reflected, 0xEDB88320) — the check on every <c>.partial</c> chunk. Table-driven, no dependency.</summary>
internal static class Crc32
{
    private static readonly uint[] _table = BuildTable();

    public static uint Of(ReadOnlySpan<byte> bytes) => Append(0xFFFFFFFFu, bytes) ^ 0xFFFFFFFFu;

    /// <summary>Feed more bytes into a running check; start from <c>0xFFFFFFFF</c> and finish with <c>^ 0xFFFFFFFF</c>.</summary>
    public static uint Append(uint state, ReadOnlySpan<byte> bytes)
    {
        foreach (byte b in bytes)
        {
            state = _table[(state ^ b) & 0xFF] ^ (state >> 8);
        }

        return state;
    }

    private static uint[] BuildTable()
    {
        var table = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }

            table[i] = c;
        }

        return table;
    }
}
