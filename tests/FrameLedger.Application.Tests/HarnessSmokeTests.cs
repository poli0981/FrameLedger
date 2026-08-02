namespace FrameLedger.Application.Tests;

/// <summary>
/// Proves the xUnit v3 harness discovers and runs tests in this project.
/// </summary>
/// <remarks>
/// Scaffolding. Delete this once real tests exist here — but not before:
/// an empty test project reports "No test is available", which makes the
/// <c>dotnet test</c> gate in build.ps1 pass without having verified anything.
/// docs/14_TESTING.md says the golden metric tests are written FIRST.
/// </remarks>
public sealed class HarnessSmokeTests
{
    [Fact]
    public void HarnessDiscoversAndRunsTests() => Assert.True(true);
}
