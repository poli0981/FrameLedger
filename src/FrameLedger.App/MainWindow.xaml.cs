using System.Windows;

namespace FrameLedger.App;

/// <summary>
/// Shell window.
/// </summary>
/// <remarks>
/// Scaffold only. The real shell is a <c>ui:FluentWindow</c> with a custom
/// <c>ui:TitleBar</c>, a classic <c>Menu</c> row **outside** the title bar's
/// drag region, and a left <c>ui:NavigationView</c>
/// (<c>docs/08_UI.md</c>, <c>docs/16_WPFUI_SYNTAX.md</c> §Main window skeleton).
/// </remarks>
public partial class MainWindow : Window
{
    /// <summary>Initializes the shell window.</summary>
    public MainWindow()
    {
        InitializeComponent();
    }
}
