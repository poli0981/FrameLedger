using FluentAssertions;
using FrameLedger.CaptureHost.Consume;
using FrameLedger.Shared;

namespace FrameLedger.CaptureHost.Tests.Consume;

/// <summary>
/// The census §S30's decision table reads, and the one bucket that could silently be zero.
/// </summary>
public sealed class SlCensusTests
{
    private static FlFrameRecord Batch(byte upscaler, FlFeatureFlags flags, byte fgEvaluations = 0) => new()
    {
        SwapchainId = 1,
        MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler),
        Upscaler = upscaler,
        FgEvaluations = fgEvaluations,
        FeatureFlags = (byte)(flags | FlFeatureFlags.RayReconstructionObserved),
    };

    [Fact]
    public void AnUndecodedIdIsCountedEvenWhenItSHARESAPresentWithFrameGeneration()
    {
        // THE WHOLE REASON FL_FEAT_SL_UNDECODED EXISTS. Once frame generation is carried as
        // a count rather than a feature bit, a present holding kFeatureDLSS_G alongside an
        // id that fell to FL_SL_SEEN_OTHER — Reflex, which Cyberpunk runs — is byte-identical
        // to one holding DLSS-G alone. Deriving the bucket from "fgEvaluations == 0" would
        // therefore read ZERO on a title evaluating an undecoded id every application frame,
        // and §S30's table would be settled on a number that could not have been anything
        // else. This is the case that makes the bucket reachable.
        List<FlFrameRecord> stream = [];
        for (int i = 0; i < 20; i++)
        {
            stream.Add(Batch((byte)FlUpscaler.Unknown, FlFeatureFlags.SlUndecoded, fgEvaluations: 1));
        }

        SlCensus c = SlCensus.From(stream);

        c.Batches.Should().Be(20);
        c.WithUndecoded.Should().Be(20, "the undecoded fact must survive co-occurring with a count");
        c.WithFrameGeneration.Should().Be(20);
        c.FgWithoutSuperResolution.Should().Be(20, "this is §S30's exact shape");
    }

    [Fact]
    public void ASuperResolutionIdKeepsAPresentOutOfTheS30Bucket()
    {
        // The other direction, and without it the bucket above is satisfied by any FG title.
        List<FlFrameRecord> stream = [];
        for (int i = 0; i < 20; i++)
        {
            stream.Add(Batch((byte)FlUpscaler.Dlss, FlFeatureFlags.RayReconstruction, fgEvaluations: 1));
        }

        SlCensus c = SlCensus.From(stream);

        c.WithDlss.Should().Be(20);
        c.WithRayReconstruction.Should().Be(20);
        c.FgWithoutSuperResolution.Should().Be(0);
    }

    [Fact]
    public void APresentThatDrainedNOTHINGIsNotABatch()
    {
        // The writer sets RayReconstructionObserved under `seen != 0` and nothing else, so a
        // present that consumed an empty word is not evidence about any id. Counting it would
        // dilute every bucket by the FG factor on a frame-generating title.
        List<FlFrameRecord> stream = [];
        for (int i = 0; i < 20; i++)
        {
            stream.Add(Batch((byte)FlUpscaler.Dlss, FlFeatureFlags.None, fgEvaluations: 1));
            stream.Add(new FlFrameRecord
            {
                SwapchainId = 1,
                MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler),
                Upscaler = (byte)FlUpscaler.Unknown,
                FeatureFlags = (byte)FlFeatureFlags.None,
            });
        }

        SlCensus c = SlCensus.From(stream);

        c.Records.Should().Be(40);
        c.Batches.Should().Be(20);
        c.WithDlss.Should().Be(20);
    }

    [Fact]
    public void NoIdentityHookMeansNoCensusRatherThanAnEmptyOne()
    {
        List<FlFrameRecord> stream =
        [
            new() { SwapchainId = 1, MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs) },
        ];

        SlCensus.From(stream).Describe().Should().Contain("no record claimed FL_MEASURED_UPSCALER");
    }
}
