using System.IO.MemoryMappedFiles;
using System.Text;
using FluentAssertions;
using FrameLedger.Infrastructure.Ipc;
using FrameLedger.Shared;

namespace FrameLedger.Infrastructure.Tests;

/// <summary>
/// The drain, against a mapping this test builds itself.
/// <para>
/// <c>14_TESTING</c> §Ring buffer requires exactly this: the torn-record and overwrite-oldest branches
/// are not reachable on demand from a live Overlay — you cannot ask a game to tear a record — so a test
/// driven only by <c>hook-harness</c> would exercise the happy path and leave both protocol branches
/// with no input that makes them go red. Here the test IS the writer and can produce each state
/// deliberately.
/// </para>
/// <para>
/// What this deliberately does NOT claim to cover: the second <c>seq</c> re-read, which fires only when
/// the writer overwrites a slot during the reader's own 64-byte copy. The native suite recorded that
/// two attempts to close it — a poisoned even <c>seq</c> and an 8-slot hostile writer — both failed to
/// reach it (<c>ring_test.cpp</c>). Widening the writer's window does not help; the branch is here
/// because the protocol requires it, not because a test proves it.
/// </para>
/// </summary>
public sealed class ShmRingReaderTests
{
    private const string _buildId = "v0.1.0-test-abcdef123456";
    private const uint _capacity = 8u;    // small, so drops need 9 records rather than 8193

    /// <summary>A mapping shaped like the Overlay's, which the test then writes as if it were one.</summary>
    private sealed unsafe class FakeRing : IDisposable
    {
        private readonly MemoryMappedFile _mmf;
        private readonly MemoryMappedViewAccessor _view;
        private readonly byte* _base;

        public FakeRing(int pid, uint capacity = _capacity, string buildId = _buildId)
        {
            long bytes = ShmLayout.SizeForCapacity(capacity);
            _mmf = MemoryMappedFile.CreateNew($@"Local\FrameLedger.Ring.{pid}", bytes);
            _view = _mmf.CreateViewAccessor(0, 0, MemoryMappedFileAccess.ReadWrite);

            byte* p = null;
            _view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
            _base = p;

            var h = (FlShmHandshake*)(_base + ShmLayout.HandshakeOffset);
            h->RecordSize = 64;
            h->Capacity = capacity;
            h->Pid = (uint)pid;
            h->QpcEpoch = 1;

            byte[] id = Encoding.ASCII.GetBytes(buildId);
            for (int i = 0; i < id.Length && i < 31; i++)
            {
                h->BuildId[i] = id[i];
            }

            // LAST, as the Overlay does: it is the field a reader validates first.
            h->LayoutVersion = ShmLayout.LayoutVersion;
        }

        public FlWriterState* State => (FlWriterState*)(_base + ShmLayout.WriterOffset);

        public FlControlBlock* Control => (FlControlBlock*)(_base + ShmLayout.ControlOffset);

        public FlFrameRecord* Slot(ulong index) =>
            &((FlFrameRecord*)(_base + ShmLayout.RingOffset))[index & (_capacity - 1)];

        /// <summary>Publishes one well-formed record, seqlock and all.</summary>
        public void Publish(uint frameIndex)
        {
            ulong idx = State->WriteIndex;
            FlFrameRecord* slot = Slot(idx);
            uint s = slot->Seq;
            slot->Seq = s + 1;                       // odd: writing
            slot->FrameIndex = frameIndex;
            slot->Qpc = 1000 + frameIndex;
            slot->MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs);

            // v3: the rtFlags bits are *_OBSERVED, so 0 is the honest value and FlMeasured.Rt staying
            // clear is what says nobody looked. The old opt-in NotMeasured bit is retired.
            slot->RtFlags = (byte)FlRtFlags.None;
            slot->Seq = s + 2;                       // even: complete
            State->WriteIndex = idx + 1;
        }

        public void Dispose()
        {
            _view.SafeMemoryMappedViewHandle.ReleasePointer();
            _view.Dispose();
            _mmf.Dispose();
        }
    }

    private static int FakePid() => 0x7F000000 + Environment.CurrentManagedThreadId;

    [Fact]
    public void ItDrainsWhatTheWriterPublished()
    {
        // GREEN first. A drain that returns nothing satisfies every "no bad records" assertion below.
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out ShmAttachRefusal refusal);

        refusal.Should().Be(ShmAttachRefusal.Ok);
        reader.Should().NotBeNull();

        for (uint i = 0; i < 5; i++)
        {
            ring.Publish(i);
        }

        var buffer = new FlFrameRecord[16];
        DrainResult r = reader!.Drain(buffer);

        r.Copied.Should().Be(5);
        r.Gaps.Should().Be(0);
        r.Dropped.Should().Be(0);
        buffer[0].FrameIndex.Should().Be(0);
        buffer[4].FrameIndex.Should().Be(4);
    }

    [Fact]
    public unsafe void ATornSlotBecomesAGapAndIsNotCopied()
    {
        // 07_IPC: a skipped torn record is a DATA GAP, not a missing frame. Dropping it silently merges
        // the two surrounding frame times into one double-length interval — it fabricates a stutter.
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        ring.Publish(0);
        ring.Publish(1);

        // Leave slot 1 mid-write: odd seq, exactly what the writer's first store produces.
        ring.Slot(1)->Seq |= 1u;

        ring.Publish(2);

        var buffer = new FlFrameRecord[16];
        var gaps = new List<ulong>();
        DrainResult r = reader!.Drain(buffer, gaps);

        r.Gaps.Should().Be(1, "the torn slot must be reported, not silently skipped");
        gaps.Should().ContainSingle().Which.Should().Be(1ul, "the gap carries the ring index it happened at");
        r.Copied.Should().Be(2, "the torn record must not be handed on as a frame");
        buffer[0].FrameIndex.Should().Be(0);
        buffer[1].FrameIndex.Should().Be(2);
    }

    [Fact]
    public unsafe void OverwrittenRecordsAreCountedAndTheReaderResumesAtTheOldestSurvivor()
    {
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        // The writer ran ahead by 5 more than the ring can hold. Those 5 are gone, and only the reader
        // knows it: the writer has no read index and cannot know whether a slot it overwrote was taken.
        ring.State->WriteIndex = _capacity + 5;

        var buffer = new FlFrameRecord[32];
        DrainResult r = reader!.Drain(buffer);

        r.Dropped.Should().Be(5);
        reader.TotalDropped.Should().Be(5);
        r.Copied.Should().Be((int)_capacity, "everything still in the ring is still readable");
    }

    [Fact]
    public unsafe void AttachingToARingInFlightIsNotAStall()
    {
        // fl_ring.h's reader starts at 0 because it is created with its writer. An Agent attaching later
        // is a different case, and starting at 0 would charge the writer's whole history to the drop
        // counter — which 04_CAPTURE defines as "the Agent stalled for over ~16 s" and surfaces as a
        // session warning. It would also re-ingest up to `capacity` stale records as current frames.
        int pid = FakePid();
        using var ring = new FakeRing(pid);

        ring.State->WriteIndex = 1_000_000;

        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out ShmAttachRefusal refusal);
        refusal.Should().Be(ShmAttachRefusal.Ok);

        var buffer = new FlFrameRecord[32];
        DrainResult r = reader!.Drain(buffer);

        r.Dropped.Should().Be(0, "a writer that was already running is not a stall we caused");
        r.Copied.Should().Be(0, "and its history is not a fresh capture");
        reader.RecordsBeforeAttach.Should().Be(1_000_000);
    }

    [Fact]
    public unsafe void ARefusalSetsTheStopFlagBeforeTheTickThatWouldRefreshTheDeadline()
    {
        // THE PUBLISH ORDER IS THE SAFETY PROPERTY. The Overlay reads unhookRequested and guardTicks as
        // two independent loads; a fresh tick RESETS its supervision clock. Publishing the tick first
        // and dying before the flag lands would give an unsupervised, un-stopped capture side a further
        // 65 s measured from the moment anti-cheat was DETECTED — worse than never having scanned.
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        reader!.PublishGuardResult(completedEvaluations: 7, unhookRequested: true);

        ring.Control->UnhookRequested.Should().Be(1u);
        ring.Control->GuardTicks.Should().Be(7u);

        // WHAT THIS DOES NOT PROVE, stated rather than implied: the ORDER. Both assertions above hold
        // for a publisher that writes them the wrong way round. Proving the order needs the publisher
        // killed between the two stores, which an in-process test cannot arrange without putting a seam
        // in shipped code. The property is held by PublishGuardResult being the only writer of either
        // field, not by this test — the same way the native suite records that its `g_observing` canary
        // came back green.
    }

    [Fact]
    public unsafe void AnAllowingScanAdvancesTheTickAndLeavesTheStopAlone()
    {
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        reader!.PublishGuardResult(completedEvaluations: 3, unhookRequested: false);

        ring.Control->GuardTicks.Should().Be(3u);
        ring.Control->UnhookRequested.Should().Be(0u, "a clean scan must never set the stop");
    }

    [Fact]
    public void ABuildIdMismatchRefusesInsteadOfAttaching()
    {
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, "a-different-build", out ShmAttachRefusal refusal);

        refusal.Should().Be(ShmAttachRefusal.BuildIdMismatch);
        reader.Should().BeNull("a refusal must not hand back a usable reader");
    }

    [Fact]
    public void NoMappingIsIncompleteAndNotAnEmptyRing()
    {
        // "Nothing is hooked there" and "attached, and it has produced nothing" are different states,
        // and only one of them may proceed to report a session.
        using ShmRingReader? reader = ShmRingReader.TryAttach(0x7FFFFFFE, _buildId, out ShmAttachRefusal refusal);

        refusal.Should().Be(ShmAttachRefusal.Incomplete);
        reader.Should().BeNull();
    }

    [Fact]
    public unsafe void PauseAndResumeBothLandInTheControlBlock()
    {
        // SetPaused had NO TEST AT ALL: a tree-wide grep returned exactly one code hit, its own
        // definition. Both halves of the mechanism have existed since #46 — MayObserve() reads
        // pauseRequested on the present path — and the managed writer had never been driven in either
        // direction, so nothing said the byte lands where fl_shm.h puts it.
        //
        // BOTH DIRECTIONS, because pause is the ONE control-block signal that is not one-way: a
        // publisher that stored 1 unconditionally would satisfy the true half on its own, and the
        // resume half is what the Agent needs to end a pause it started.
        //
        // AND unhookRequested is asserted untouched throughout. The two fields are four bytes apart
        // (@0 and @4) and a publisher that wrote the wrong one would set the SAFETY STOP instead of a
        // pause — the failure this assertion exists to name, rather than leaving it to be inferred
        // from a pause that "did not work".
        //
        // This needs no native fixture and carries no Integration trait, so it runs in the MERGE GATE.
        // That is not a weaker proof than a cross-process test: this Fact proves the byte lands at
        // ShmLayout.ControlOffset + 0, ShmLayoutMirrorTests proves that offset equals fl_shm.h's, and
        // ctest fl_guard proves the Overlay reads fl_shm.h's field. Every link is gated.
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        ring.Control->PauseRequested.Should().Be(0u, "nothing has asked for a pause yet");

        reader!.SetPaused(true);
        ring.Control->PauseRequested.Should().Be(1u);
        ring.Control->UnhookRequested.Should().Be(0u, "pausing is not stopping");

        reader.SetPaused(false);
        ring.Control->PauseRequested.Should().Be(0u, "a pause the Agent started is a pause it can end");
        ring.Control->UnhookRequested.Should().Be(0u);
    }

    [Fact]
    public unsafe void TheWriterStateIsReadableSoTheOverlaysOwnStopsAreVisible()
    {
        // status and faultCount are the ONLY fields carrying the three-fault self-disable, a failed hook
        // install, and the acknowledged safety unhook. A drain that reads neither reports the same
        // result for a healthy session and for a DLL that never hooked anything.
        int pid = FakePid();
        using var ring = new FakeRing(pid);
        using ShmRingReader? reader = ShmRingReader.TryAttach(pid, _buildId, out _);

        ring.State->Status = (uint)FlStatus.SelfDisabled;
        ring.State->FaultCount = 3;

        reader!.WriterState.Status.Should().Be((uint)FlStatus.SelfDisabled);
        reader.WriterState.FaultCount.Should().Be(3u);
    }
}
