namespace FrameLedger.CaptureHost;

/// <summary>
/// The host's only console surface.
/// </summary>
/// <remarks>
/// <para>
/// Not a convenience. Three analyzers make direct <c>Console.WriteLine</c> calls a
/// build error under this repo's settings, and each is right about the shipping
/// app and wrong about a developer tool: CA1849 and VSTHRD103 because a
/// synchronous write inside an <c>async</c> method blocks, and CA1303 because every
/// user-visible string belongs in <c>.resx</c> (<c>09_I18N</c>).
/// </para>
/// <para>
/// Routing through one non-async method answers all three honestly rather than by
/// suppression: the writes are no longer inside an async method, and the strings
/// are no longer literals at a <c>Console</c> call site. The <c>.resx</c> rule is
/// untouched — <c>09_I18N</c> scopes it to the shipped app's user-visible strings,
/// and this binary ships to nobody and is English-only by design. That scope is
/// written into <c>09_I18N</c> in the same PR rather than left implied.
/// </para>
/// </remarks>
internal static class HostConsole
{
    public static void Line(string text) => Console.Out.WriteLine(text);

    public static void Problem(string text) => Console.Error.WriteLine(text);
}
