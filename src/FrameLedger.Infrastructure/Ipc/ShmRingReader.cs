using System.IO.MemoryMappedFiles;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Ipc;

/// <summary>
/// Reads the Overlay's frame ring out of <c>Local\FrameLedger.Ring.&lt;pid&gt;</c>, and owns the
/// Agent's half of the control block.
/// <para>
/// The protocol is <c>fl_ring.h</c>'s <c>RingReader::Drain</c>, mirrored rather than re-derived: where
/// the two disagree the header is right and this file is the bug.
/// </para>
/// </summary>
/// <remarks>
/// <para>
/// <b>The view is mapped at offset 0 and every region is reached as <c>base + RegionOffset</c>. That is
/// not a convenience; the obvious alternative is silently wrong.</b>
/// </para>
/// <para>
/// Measured 2026-08-05 (.NET 10, Windows 11 26300): a view created at a non-zero offset is mapped from
/// the 64 KiB allocation-granularity boundary <i>below</i> it, and
/// <c>SafeMemoryMappedViewHandle.AcquirePointer</c> returns <i>that</i> base — not the offset asked for.
/// <c>CreateViewAccessor(0x80, 0x40, ReadWrite)</c> yields <c>PointerOffset = 128</c> and a pointer to
/// mapping byte 0, with <c>ByteLength = 192</c> spanning from zero.
/// </para>
/// <para>
/// So "map region 3 read-write at <c>0x80</c>, write <c>*(uint*)(p + 12)</c>" writes
/// <c>FlShmHandshake.pid</c> instead of <c>FlControlBlock.guardTicks</c> — the supervision counter onto
/// the field a reader validates first. It does not fault, and
/// <c>SafeMemoryMappedViewHandle.Read&lt;T&gt;</c> does not bounds-check it either, because the handle
/// legitimately spans from byte 0. Mapping at zero removes <c>PointerOffset</c> from the arithmetic
/// entirely and makes fl_shm.h's offsets usable as written, which is what <c>RingWriter::Init</c> and
/// <c>RingReader::Init</c> already do natively. <c>Bind</c> asserts it rather than assuming it.
/// </para>
/// </remarks>
public sealed unsafe class ShmRingReader : IDisposable
{
    private readonly MemoryMappedFile _mmf;
    private readonly MemoryMappedViewAccessor _view;
    private readonly byte* _base;
    private readonly uint _capacity;
    private readonly uint _mask;

    private ulong _readIndex;
    private bool _pointerHeld;
    private bool _disposed;

    private ShmRingReader(MemoryMappedFile mmf, MemoryMappedViewAccessor view, byte* basePtr, uint capacity)
    {
        _mmf = mmf;
        _view = view;
        _base = basePtr;
        _capacity = capacity;
        _mask = capacity - 1;
        _pointerHeld = true;
    }

    /// <summary>Total records overwritten before we consumed them, across every drain.</summary>
    public long TotalDropped { get; private set; }

    /// <summary>Total gap markers across every drain.</summary>
    public long TotalGaps { get; private set; }

    /// <summary>
    /// Records the writer had already published when we attached, which are NOT drops.
    /// <para>
    /// Kept separate on purpose. <c>04_CAPTURE</c> §Ring draining defines a non-zero drop count as
    /// "the Agent stalled for over ~16 s" and requires a session warning, so charging a writer's
    /// pre-attach history to it would make that warning's verdict depend on how long the target had
    /// been presenting before anyone looked. Attaching to a ring already in flight is not a stall.
    /// </para>
    /// </summary>
    public ulong RecordsBeforeAttach { get; private init; }

    /// <summary>
    /// Opens the ring for <paramref name="pid"/> and validates the handshake.
    /// Returns null with <paramref name="refusal"/> set when it must not be attached to.
    /// </summary>
    public static ShmRingReader? TryAttach(int pid, string ownBuildId, out ShmAttachRefusal refusal)
    {
        refusal = ShmAttachRefusal.NotEvaluated;

        MemoryMappedFile? mmf = null;
        MemoryMappedViewAccessor? view = null;
        try
        {
            mmf = OpenRing(pid);
            if (mmf is null)
            {
                // No mapping: nothing is hooked in that process. Not an error, and NOT "attached with
                // an empty ring" — different states, and only one of them may proceed.
                refusal = ShmAttachRefusal.Incomplete;
                return null;
            }

            view = mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.ReadWrite);
            ShmRingReader? reader = Bind(mmf, view, ownBuildId, out refusal);
            if (reader is not null)
            {
                mmf = null;     // ownership handed to the reader
                view = null;
            }

            return reader;
        }
        finally
        {
            view?.Dispose();
            mmf?.Dispose();
        }
    }

    private static MemoryMappedFile? OpenRing(int pid)
    {
        try
        {
            // ReadWrite: region 3 is ours to write. Windows cannot grant that per-region, so the rights
            // cover the section and the containment is in the code — the control block is reached only
            // through the publishers below, never through a caller-supplied offset.
            return MemoryMappedFile.OpenExisting(
                $@"Local\FrameLedger.Ring.{pid}", MemoryMappedFileRights.ReadWrite);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
    }

    private static ShmRingReader? Bind(
        MemoryMappedFile mmf, MemoryMappedViewAccessor view, string ownBuildId, out ShmAttachRefusal refusal)
    {
        // PointerOffset MUST be 0 — see the class remarks. Everything downstream indexes from `_base`
        // with the offsets in fl_shm.h, and that is only correct for a view mapped at zero.
        if (view.PointerOffset != 0)
        {
            refusal = ShmAttachRefusal.Incomplete;
            return null;
        }

        byte* p = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
        if (p == null)
        {
            refusal = ShmAttachRefusal.Incomplete;
            return null;
        }

        bool handedOff = false;
        try
        {
            long mappedBytes = (long)view.SafeMemoryMappedViewHandle.ByteLength;
            FlShmHandshake handshake = *(FlShmHandshake*)(p + ShmLayout.HandshakeOffset);

            refusal = ShmHandshakeValidator.Validate(handshake, ownBuildId, mappedBytes);
            if (refusal != ShmAttachRefusal.Ok)
            {
                return null;
            }

            // SEED THE READ INDEX FROM THE WRITER, never from zero. fl_ring.h's reader starts at 0
            // because it is created alongside its writer; an Agent attaching to a ring already in flight
            // is a different situation. Starting at 0 charges the writer's whole history to the drop
            // counter — which 04_CAPTURE defines as "the Agent stalled for over ~16 s" — and worse,
            // re-ingests up to `capacity` STALE records as current frames. StopObserving is one-way, so
            // a ring left behind by a finished session still holds its records with writeIndex frozen.
            ulong writeIndex = Volatile.Read(ref *(ulong*)(p + ShmLayout.WriterOffset));

            var reader = new ShmRingReader(mmf, view, p, handshake.Capacity)
            {
                _readIndex = writeIndex,
                RecordsBeforeAttach = writeIndex,
            };
            handedOff = true;
            return reader;
        }
        finally
        {
            if (!handedOff)
            {
                view.SafeMemoryMappedViewHandle.ReleasePointer();
            }
        }
    }

    /// <summary>Region 2, read fresh. The only place the Overlay's own stops are visible.</summary>
    public FlWriterState WriterState => *(FlWriterState*)(_base + ShmLayout.WriterOffset);

    /// <summary>Region 1, read fresh.</summary>
    public FlShmHandshake Handshake => *(FlShmHandshake*)(_base + ShmLayout.HandshakeOffset);

    /// <summary>
    /// Copies up to <paramref name="into"/>.Length records. <paramref name="gapIndices"/> receives the
    /// ring index of each torn slot, so the caller can record a gap rather than a missing frame.
    /// </summary>
    public DrainResult Drain(Span<FlFrameRecord> into, IList<ulong>? gapIndices = null)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        int copied = 0;
        int gaps = 0;
        long dropped = 0;

        ulong writeIndex = Volatile.Read(ref *(ulong*)(_base + ShmLayout.WriterOffset));

        // Overwrite-oldest accounting, owned HERE because only the reader knows what it consumed: the
        // writer has no read index and cannot know whether the slot it overwrites was ever taken.
        if (writeIndex > _readIndex + _capacity)
        {
            dropped = (long)(writeIndex - _readIndex - _capacity);
            _readIndex = writeIndex - _capacity;
        }

        var ring = (FlFrameRecord*)(_base + ShmLayout.RingOffset);

        while (_readIndex < writeIndex && copied < into.Length)
        {
            FlFrameRecord* slot = &ring[_readIndex & _mask];

            uint before = Volatile.Read(ref slot->Seq);
            if ((before & 1u) != 0u)
            {
                // Odd: a write is in flight. A gap, recorded at its index.
                gapIndices?.Add(_readIndex);
                gaps++;
                _readIndex++;
                continue;
            }

            into[copied] = *slot;

            // Re-read seq AFTER the copy. Unequal means the writer lapped us or overwrote mid-copy;
            // either way the bytes we hold are not one frame. Volatile.Read gives the acquire the
            // writer's release fences are paired with.
            if (Volatile.Read(ref slot->Seq) != before)
            {
                gapIndices?.Add(_readIndex);
                gaps++;
                _readIndex++;
                continue;
            }

            copied++;
            _readIndex++;
        }

        TotalDropped += dropped;
        TotalGaps += gaps;
        return new DrainResult(copied, gaps, dropped);
    }

    /// <summary>
    /// Publishes one completed guard evaluation, and the refusal if there was one.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <b>The order is the safety property, and it is the reverse of the natural one.</b>
    /// <c>GuardSupervisor</c> increments its counter before it latches the refusal, so a publisher that
    /// mirrors its state field by field would write the tick first. The Overlay reads the two as
    /// independent loads: if the tick lands and the flag never does — the host is killed, faults, or
    /// throws in between — the watchdog sees a fresh tick, resets its clock and keeps observing for a
    /// further 65 s measured from the moment anti-cheat was DETECTED. Refusing would then be worse than
    /// never having scanned, whose deadline was already running.
    /// </para>
    /// <para>So: the flag first, always, and the tick only after it is in the mapping.</para>
    /// <para>
    /// <b>The ORDER is not verified by the suite, and saying so is cheaper than letting a reader assume
    /// it is.</b> <c>ShmRingReaderTests</c> asserts that both values land, which a publisher writing
    /// them in the wrong order also satisfies. Proving the order needs the publisher killed between the
    /// two stores, and an in-process test cannot do that without a seam in shipped code. What holds the
    /// property is this method being the only writer of either field.
    /// </para>
    /// </remarks>
    public void PublishGuardResult(uint completedEvaluations, bool unhookRequested)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        var control = (FlControlBlock*)(_base + ShmLayout.ControlOffset);

        if (unhookRequested)
        {
            Volatile.Write(ref control->UnhookRequested, 1u);
        }

        Volatile.Write(ref control->GuardTicks, completedEvaluations);
    }

    /// <summary>Asks the capture side to hold without stopping. The supervision clock keeps running.</summary>
    public void SetPaused(bool paused)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var control = (FlControlBlock*)(_base + ShmLayout.ControlOffset);
        Volatile.Write(ref control->PauseRequested, paused ? 1u : 0u);
    }

    /// <summary>
    /// Ask the capture side to flush its native event ring to <c>logs\overlay-&lt;pid&gt;-*.log</c> now
    /// (<c>17_HOOK_ENGINE</c> §Native logging). A counter: the watchdog acts on a change within one tick,
    /// and this side never clears anything.
    /// </summary>
    public void RequestLogFlush()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var control = (FlControlBlock*)(_base + ShmLayout.ControlOffset);
        Volatile.Write(ref control->LogFlushRequested, Volatile.Read(ref control->LogFlushRequested) + 1u);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        if (_pointerHeld)
        {
            _view.SafeMemoryMappedViewHandle.ReleasePointer();
            _pointerHeld = false;
        }

        _view.Dispose();
        _mmf.Dispose();
    }
}
