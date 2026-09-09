using FluentAssertions;
using FrameLedger.Domain.Metrics;
using FrameLedger.Shared;

namespace FrameLedger.Application.Tests.Metrics;

/// <summary>
/// Every Domain enum is numerically identical to its <c>FrameLedger.Shared</c> twin, walked in BOTH
/// directions — a member added on either side alone fails, the way <c>ShmLayoutMirrorTests</c> holds the
/// struct mirror. This is what makes <c>FrameSampleMapper</c>'s casts exact rather than hopeful.
/// </summary>
public sealed class MetricEnumMirrorTests
{
    public static TheoryData<Type, Type> Pairs => new()
    {
        { typeof(MeasuredFields), typeof(FlMeasured) },
        { typeof(FrameApi), typeof(FlApi) },
        { typeof(UpscalerKind), typeof(FlUpscaler) },
        { typeof(FgKind), typeof(FlFgMode) },
        { typeof(ColorSpaceKind), typeof(FlColorSpace) },
        { typeof(FeatureBits), typeof(FlFeatureFlags) },
        { typeof(HookFamilies), typeof(FlHookFamily) },
        { typeof(RuntimeCensusBits), typeof(FlRuntimeCensus) },
        { typeof(RtEvidenceBits), typeof(FlRtFlags) },
        { typeof(RtTierValue), typeof(FlRtTier) },
    };

    [Theory]
    [MemberData(nameof(Pairs))]
    public void EveryMemberExistsOnBothSidesWithTheSameValue(Type domain, Type shared)
    {
        ArgumentNullException.ThrowIfNull(domain);
        ArgumentNullException.ThrowIfNull(shared);

        Dictionary<string, long> d = Members(domain);
        Dictionary<string, long> s = Members(shared);

        d.Keys.Should().BeEquivalentTo(s.Keys, $"{domain.Name} and {shared.Name} must name the same members");
        foreach ((string name, long value) in d)
        {
            s[name].Should().Be(value, $"{domain.Name}.{name} must carry {shared.Name}'s value");
        }
    }

    [Fact]
    public void TheFamiliesAreTheSameUnions()
    {
        ((long)RuntimeCensusFamilies.Fg).Should().Be((long)FlRuntimeCensusFamilies.Fg);
        ((long)RuntimeCensusFamilies.Upscaler).Should().Be((long)FlRuntimeCensusFamilies.Upscaler);
    }

    [Fact]
    public void EveryMirroredEnumIsCoveredByAPair()
    {
        // A new enum in Domain.Metrics that mirrors nothing is fine; one that mirrors a Shared enum and is
        // not in the table above would be a mirror nobody checks. Enumerate Domain.Metrics' enums and
        // require each to be either paired or explicitly local.
        string[] local = [nameof(Tri), nameof(FgRefusalKind), nameof(FgRefusalSubject)];
        IEnumerable<string> enums = typeof(FrameSample).Assembly.GetTypes()
            .Where(t => t.IsEnum && string.Equals(t.Namespace, typeof(FrameSample).Namespace, StringComparison.Ordinal))
            .Select(t => t.Name);
        IEnumerable<string> paired = Pairs.Select(row => row.Data.Item1.Name);

        enums.Should().BeSubsetOf([.. paired, .. local]);
    }

    private static Dictionary<string, long> Members(Type e) =>
        Enum.GetNames(e).ToDictionary(n => n, n => Convert.ToInt64(Enum.Parse(e, n), System.Globalization.CultureInfo.InvariantCulture), StringComparer.Ordinal);
}
