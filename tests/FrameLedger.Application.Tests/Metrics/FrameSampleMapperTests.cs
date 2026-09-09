using FluentAssertions;
using FrameLedger.Application.Metrics;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Metrics;

/// <summary>Field by field, and the two sentinels resolved to null exactly once.</summary>
public sealed class FrameSampleMapperTests
{
    [Fact]
    public void EveryConsumedFieldOfTheRecordReachesTheSample()
    {
        var r = new FlFrameRecord
        {
            Qpc = 123_456_789,
            FrameIndex = 42,
            SwapchainId = 7,
            Api = (byte)FlApi.D3D12,
            PresentFlags = 0x200,
            SyncInterval = 1,
            RenderW = 1485,
            RenderH = 835,
            OutputW = 2560,
            OutputH = 1440,
            Upscaler = (byte)FlUpscaler.Dlss,
            UpscalerQuality = 0xFF,
            UpscalerSharpness = 0xFF,
            FgMode = (byte)FlFgMode.DlssG,
            FgEvaluations = 1,
            DxgiUnseen = 3,
            RtFlags = (byte)(FlRtFlags.AsBuildObserved | FlRtFlags.DispatchObserved),
            DispatchRaysVolume = 8_294_400,
            MaxTraceRecursionDepth = 2,
            PsoCreatedThisFrame = 5,
            FeatureFlags = (byte)(FlFeatureFlags.RayReconstruction | FlFeatureFlags.RayReconstructionObserved),
            ColorSpace = (byte)FlColorSpace.Hdr10,
            VramUsedMb = 9000,
            ReflexLatencyUs = 12_000,
            MeasuredMask = (ushort)(FlMeasured.OutputRes | FlMeasured.PresentArgs | FlMeasured.Upscaler | FlMeasured.Rt),
        };

        FrameSample s = FrameSampleMapper.Map(r);

        s.Qpc.Should().Be(123_456_789);
        s.FrameIndex.Should().Be(42);
        s.SwapchainId.Should().Be(7);
        s.Api.Should().Be(FrameApi.D3D12);
        s.PresentFlags.Should().Be(0x200);
        s.SyncInterval.Should().Be(1);
        (s.RenderW, s.RenderH, s.OutputW, s.OutputH).Should().Be(((ushort)1485, (ushort)835, (ushort)2560, (ushort)1440));
        s.Upscaler.Should().Be(UpscalerKind.Dlss);
        s.UpscalerQuality.Should().Be(0xFF);
        s.UpscalerSharpness.Should().Be(0xFF);
        s.FgMode.Should().Be(FgKind.DlssG);
        s.FgEvaluations.Should().Be(1);
        s.DxgiUnseen.Should().Be(3);
        s.Rt.Should().Be(RtEvidenceBits.AsBuildObserved | RtEvidenceBits.DispatchObserved);
        s.DispatchRaysVolume.Should().Be(8_294_400);
        s.MaxTraceRecursionDepth.Should().Be(2);
        s.PsoCreated.Should().Be(5);
        s.Features.Should().Be(FeatureBits.RayReconstruction | FeatureBits.RayReconstructionObserved);
        s.ColorSpace.Should().Be(ColorSpaceKind.Hdr10);
        s.VramUsedMb.Should().Be(9000);
        s.ReflexLatencyUs.Should().Be(12_000);
        s.Measured.Should().Be(MeasuredFields.OutputRes | MeasuredFields.PresentArgs | MeasuredFields.Upscaler | MeasuredFields.Rt);
        s.Claims(MeasuredFields.Rt | MeasuredFields.Upscaler).Should().BeTrue();
        s.Claims(MeasuredFields.Fg).Should().BeFalse();
    }

    [Fact]
    public void AListMapsInOrder()
    {
        FlFrameRecord[] records = [new() { Qpc = 1 }, new() { Qpc = 2 }, new() { Qpc = 3 }];

        FrameSampleMapper.Map(records).Select(s => s.Qpc).Should().Equal(1UL, 2UL, 3UL);
        FrameSampleMapper.Map([]).Should().BeEmpty();
    }

    [Fact]
    public void TheWriterSentinelsBecomeNull()
    {
        WriterFacts none = FrameSampleMapper.Map(new FlWriterState
        {
            VramBudgetMb = 0,
            DxgiPresentsBeforeHook = FlWriterState.DxgiPresentsBeforeHookNotRead,
        });
        none.VramBudgetMb.Should().BeNull("0 means nobody wrote it");
        none.DxgiPresentsBeforeHook.Should().BeNull("all bits set means no present was seen");

        WriterFacts some = FrameSampleMapper.Map(new FlWriterState
        {
            RtTier = 11,
            HooksInstalledMask = (uint)(FlHookFamily.Present | FlHookFamily.RtAsBuild),
            RuntimeCensus = (uint)(FlRuntimeCensus.Ran | FlRuntimeCensus.SlDlssG),
            SlTagCensus = 0x1234,
            DxgiPresentsUnseen = 600,
            DxgiPresentSamples = 200,
            VramBudgetMb = 8000,
            RtStateObjectsCreated = 3,
            RasterPsoCreated = 40,
            FaultCount = 1,
            DxgiPresentsBeforeHook = 12,
        });
        some.RtTier.Should().Be(11);
        some.RtCapable.Should().BeTrue();
        some.HooksInstalled.Should().Be(HookFamilies.Present | HookFamilies.RtAsBuild);
        some.RuntimeCensus.Should().Be(RuntimeCensusBits.Ran | RuntimeCensusBits.SlDlssG);
        some.SlTagCensus.Should().Be(0x1234);
        some.DxgiPresentsUnseen.Should().Be(600);
        some.DxgiPresentSamples.Should().Be(200);
        some.VramBudgetMb.Should().Be(8000);
        some.RtStateObjectsCreated.Should().Be(3);
        some.RasterPsoCreated.Should().Be(40);
        some.FaultCount.Should().Be(1);
        some.DxgiPresentsBeforeHook.Should().Be(12);
    }
}
