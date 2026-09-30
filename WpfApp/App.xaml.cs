using System.Windows;
using Electronic_Load.Properties;

namespace Electronic_Load
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            ThemeManager.Initialize();
            ThemeManager.ApplyTheme(Settings.Default.IsDarkTheme);
            base.OnStartup(e);
        }
    }
}
