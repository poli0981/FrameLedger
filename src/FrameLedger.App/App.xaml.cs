using System.Windows;

namespace FrameLedger.App;

/// <summary>
/// Application entry point.
/// </summary>
/// <remarks>
/// Scaffold only. The real bootstrap owns everything through a .NET Generic
/// Host and must follow <c>docs/16_WPFUI_SYNTAX.md</c> exactly:
/// <list type="bullet">
///   <item>merged dictionaries in order <c>ThemesDictionary</c> →
///   <c>ControlsDictionary</c> → our styles — wrong order silently yields
///   default-looking controls;</item>
///   <item><c>SystemThemeWatcher.Watch</c> from the window's <c>Loaded</c>
///   handler, never the constructor (it needs an HWND);</item>
///   <item>navigation only via <c>INavigationService</c>, with every page
///   registered in DI.</item>
/// </list>
/// </remarks>
// FULLY QUALIFIED, deliberately. This project's namespace is FrameLedger.App
// and there is now a FrameLedger.Application namespace beside it, so the bare
// name `Application` resolves to that NAMESPACE rather than to the WPF type
// (CS0118) — sibling namespaces win over a `using`. Anything here that means
// the WPF Application must say so in full.
public partial class App : System.Windows.Application
{
}
