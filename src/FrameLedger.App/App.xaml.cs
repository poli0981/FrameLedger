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
public partial class App : Application
{
}
