using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using FluentAssertions;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// The struct-mirror gate. CLAUDE.md §Struct mirroring calls this the mechanism protecting the
/// shared-memory ABI; until 2026-08-05 nine files described it in the present tense and none of it
/// existed (<c>20_OPEN_QUESTIONS</c> §R10).
/// <para>
/// The native side is <c>tools/fl-layout-dump</c>, which is RUN rather than transcribed. A hand-written
/// offset table here would be a second statement of the layout, and two statements of one fact drift —
/// which is the defect this test exists to catch, reintroduced inside the catcher.
/// </para>
/// <para>
/// Every failure to obtain the native answer is a FAILURE, never a skip. "Could not look" reading as
/// "looked and it matched" is the defect class this repository keeps recording, and a mirror test that
/// silently skips on a machine without the native build is a gate that cannot fail.
/// </para>
/// </summary>
public sealed class ShmLayoutMirrorTests
{
    private static readonly Lazy<JsonElement> _nativeLayout = new(RunLayoutDump);

    private static JsonElement RunLayoutDump()
    {
        string exe = Path.Combine(AppContext.BaseDirectory, "fl-layout-dump.exe");
        File.Exists(exe).Should().BeTrue(
            "fl-layout-dump.exe must sit beside the test binary (FrameLedger.LayoutDump.targets). "
            + "Run build.ps1 native first. This is a FAILURE and not a skip on purpose: without the "
            + "native answer this test would assert the C# mirror against itself.");

        using var process = new Process
        {
            StartInfo = new ProcessStartInfo(exe)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };

        process.Start().Should().BeTrue("fl-layout-dump.exe must be launchable");
        string json = process.StandardOutput.ReadToEnd();
        process.WaitForExit(30_000).Should().BeTrue("fl-layout-dump.exe must exit promptly");
        process.ExitCode.Should().Be(0, "fl-layout-dump.exe must succeed, not print a partial layout");
        json.Should().NotBeNullOrWhiteSpace("an empty dump would make every offset assertion vacuous");

        // Parse failure throws, which is the intended outcome — a malformed dump must not read as clean.
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }

    private static JsonElement StructOf(string name)
    {
        JsonElement structs = _nativeLayout.Value.GetProperty("structs");
        structs.TryGetProperty(name, out JsonElement element).Should().BeTrue(
            $"fl-layout-dump must emit {name}; a struct that disappears from the dump would otherwise "
            + "silently stop being checked");
        return element;
    }

    private static void AssertMirror<T>(string nativeName)
        where T : struct
    {
        JsonElement native = StructOf(nativeName);

        // Size, both ways. Marshal.SizeOf is the marshalled size; Unsafe.SizeOf is what the runtime
        // actually lays out and therefore what a memory-mapped read sees. They can differ, and only the
        // second one is the property the reader depends on.
        int nativeSize = native.GetProperty("size").GetInt32();
        nativeSize.Should().Be(64, $"{nativeName} is one cache line in fl_shm.h");
        Marshal.SizeOf<T>().Should().Be(nativeSize, $"{typeof(T).Name} marshalled size must match {nativeName}");
        Unsafe.SizeOf<T>().Should().Be(nativeSize, $"{typeof(T).Name} runtime size must match {nativeName}");

        // BLITTABILITY, which offsets structurally cannot see.
        //
        // The repository's existing idiom for a native char[N] is
        // [MarshalAs(ByValTStr, SizeConst = N)] public string (NativeAntiCheatGuard's FlGuardResult,
        // where it is correct because that struct crosses a P/Invoke boundary and the marshaller runs).
        // Used here it would satisfy every offset assertion above and below, and then fail to read out
        // of a MemoryMappedViewAccessor — which is the one thing these types exist for.
        RuntimeHelpers.IsReferenceOrContainsReferences<T>().Should().BeFalse(
            $"{typeof(T).Name} must be blittable: it is read straight out of a memory-mapped view, and a "
            + "managed reference anywhere inside it makes that impossible. Offsets cannot detect this.");

        foreach (JsonElement field in native.GetProperty("fields").EnumerateArray())
        {
            string nativeField = field.GetProperty("name").GetString()!;
            int nativeOffset = field.GetProperty("offset").GetInt32();

            // The C# names are PascalCase mirrors of the native camelCase ones.
            string managedField = char.ToUpperInvariant(nativeField[0]) + nativeField[1..];
            typeof(T).GetField(managedField).Should().NotBeNull(
                $"{typeof(T).Name} must mirror {nativeName}.{nativeField}; a field present natively and "
                + "absent here means the Agent reads a value nobody mapped");

            int managedOffset = Marshal.OffsetOf<T>(managedField).ToInt32();
            managedOffset.Should().Be(
                nativeOffset,
                $"{typeof(T).Name}.{managedField} must sit at the same offset as {nativeName}.{nativeField}");
        }

        AssertNoGapsOrOverlaps<T>(native, nativeName);
    }

    /// <summary>
    /// Widths, which the offset walk cannot see on its own.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <c>fl-layout-dump</c> has always emitted a <c>size</c> per field and this test read only
    /// <c>name</c> and <c>offset</c>. Asserted as adjacency: every field must start where the
    /// previous one ends, and the last must end at the managed struct size. That proves the native
    /// struct has no implicit padding — a property the C++ side states in prose ("64 bytes exactly,
    /// no implicit padding") and nothing checked — and it binds the native total to the managed one.
    /// </para>
    /// <para>
    /// <b>What it does NOT catch, established by canary rather than assumed.</b> An earlier version
    /// of this comment claimed it caught "a <c>reserved</c> array whose length is wrong in a way
    /// that keeps the struct the same size". It does not, on the managed side: shrinking
    /// <c>FlWriterState.Reserved</c> from <c>[6]</c> to <c>[5]</c> left all 8 assertions green,
    /// because this walk reads the NATIVE field list and <c>[StructLayout(Size = 64)]</c> pads the
    /// managed struct back to 64 regardless. No offset moves and no reflection can see it.
    /// </para>
    /// <para>
    /// It does catch a wrong width on the native side, which is where a layout edit is made:
    /// reporting <c>measuredMask</c> as one byte instead of two fails at
    /// "must end exactly where upscalerSharpness begins", with the native build green.
    /// </para>
    /// </remarks>
    // `struct`, matching AssertMirror — not `unmanaged`. Unsafe.SizeOf<T> does not need it, and
    // requiring it here would make this helper uncallable from the very method whose blittability
    // assertion is what establishes the stronger property in the first place.
    private static void AssertNoGapsOrOverlaps<T>(JsonElement native, string nativeName)
        where T : struct
    {
        var ordered = native.GetProperty("fields").EnumerateArray()
            .Select(f => (
                Name: f.GetProperty("name").GetString()!,
                Offset: f.GetProperty("offset").GetInt32(),
                Size: f.GetProperty("size").GetInt32()))
            .OrderBy(f => f.Offset)
            .ToList();

        ordered.Should().NotBeEmpty(
            $"the dump reported no fields for {nativeName}, so this check walked an empty list "
            + "and proved nothing");

        for (int i = 0; i < ordered.Count - 1; i++)
        {
            (ordered[i].Offset + ordered[i].Size).Should().Be(
                ordered[i + 1].Offset,
                $"{nativeName}.{ordered[i].Name} must end exactly where {ordered[i + 1].Name} begins; "
                + "a gap is implicit padding the C# mirror cannot reproduce, and an overlap is a "
                + "wrong width");
        }

        (ordered[^1].Offset + ordered[^1].Size).Should().Be(
            Unsafe.SizeOf<T>(),
            $"{nativeName}.{ordered[^1].Name} must end at the end of the struct; a short trailing "
            + "field (or a `reserved` array of the wrong length) leaves bytes no side describes");
    }

    [Fact]
    public void TheLayoutVersionMatches()
    {
        // The version the Agent compares against the handshake before attaching. If this drifts, the
        // Agent refuses every session or accepts an incompatible one — and 11_UPDATER makes it a SemVer
        // MAJOR, so it must never move by accident.
        _nativeLayout.Value.GetProperty("layoutVersion").GetUInt32().Should().Be(ShmLayout.LayoutVersion);
    }

    [Fact]
    public void TheRegionOffsetsMatch()
    {
        JsonElement regions = _nativeLayout.Value.GetProperty("regions");
        regions.GetProperty("handshake").GetUInt32().Should().Be(ShmLayout.HandshakeOffset);
        regions.GetProperty("writer").GetUInt32().Should().Be(ShmLayout.WriterOffset);
        regions.GetProperty("control").GetUInt32().Should().Be(ShmLayout.ControlOffset);
        regions.GetProperty("ring").GetUInt32().Should().Be(ShmLayout.RingOffset);
    }

    [Fact]
    public void TheHandshakeMirrorMatches() => AssertMirror<FlShmHandshake>("FlShmHandshake");

    [Fact]
    public void TheWriterStateMirrorMatches() => AssertMirror<FlWriterState>("FlWriterState");

    [Fact]
    public void TheControlBlockMirrorMatches() => AssertMirror<FlControlBlock>("FlControlBlock");

    [Fact]
    public void TheFrameRecordMirrorMatches() => AssertMirror<FlFrameRecord>("FlFrameRecord");

    [Fact]
    public void EveryNativeFieldIsMirrored()
    {
        // AssertMirror walks the NATIVE field list, so a field added natively and forgotten here fails.
        // This walks the other way: a field added HERE that the dump does not emit means either the
        // mirror invented something or fl-layout-dump stopped reporting it, and the second is how the
        // check above would quietly start testing less than it claims.
        (string Native, Type Managed)[] pairs =
        [
            ("FlShmHandshake", typeof(FlShmHandshake)),
            ("FlWriterState", typeof(FlWriterState)),
            ("FlControlBlock", typeof(FlControlBlock)),
            ("FlFrameRecord", typeof(FlFrameRecord)),
        ];

        foreach ((string nativeName, Type managed) in pairs)
        {
            HashSet<string> nativeFields = StructOf(nativeName)
                .GetProperty("fields")
                .EnumerateArray()
                .Select(f => f.GetProperty("name").GetString()!)
                .Select(n => char.ToUpperInvariant(n[0]) + n[1..])
                .ToHashSet(StringComparer.Ordinal);

            nativeFields.Should().NotBeEmpty($"{nativeName} must report fields, or its assertions are vacuous");

            IEnumerable<string> managedFields = managed
                .GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance)
                .Select(f => f.Name);

            foreach (string name in managedFields)
            {
                nativeFields.Should().Contain(
                    name,
                    $"{managed.Name}.{name} has no counterpart in {nativeName} — either it was invented "
                    + "here, or fl-layout-dump stopped emitting it and the offset walk silently shrank");
            }
        }
    }

    [Fact]
    public void TheRecordSizeUsedForRingArithmeticMatches()
    {
        // SizeForCapacity is what the reader will use to bound the mapping. Deriving it from a hardcoded
        // 64 would be a third statement of the record size.
        int recordSize = StructOf("FlFrameRecord").GetProperty("size").GetInt32();
        ShmLayout.SizeForCapacity(ShmLayout.DefaultCapacity)
            .Should()
            .Be(ShmLayout.RingOffset + ((long)ShmLayout.DefaultCapacity * recordSize));
    }
}
