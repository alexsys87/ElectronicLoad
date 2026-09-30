using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace Electronic_Load
{
    /// <summary>
    /// Switches the light/dark color palette at run time.
    /// App.xaml merges one palette (Themes/Light.xaml or Themes/Dark.xaml) plus Themes/Controls.xaml;
    /// only the palette is replaced, the control templates read colors through DynamicResource.
    /// Also asks DWM for a dark window caption on Windows 10 20H1+ / Windows 11.
    /// </summary>
    public static class ThemeManager
    {
        private const string MarkerKey = "ThemeName";
        private static readonly Uri LightUri = new("pack://application:,,,/Themes/Light.xaml", UriKind.Absolute);
        private static readonly Uri DarkUri = new("pack://application:,,,/Themes/Dark.xaml", UriKind.Absolute);
        private static bool _initialized;

        public static bool IsDark { get; private set; }

        /// <summary>Raised after the palette has been replaced.</summary>
        public static event EventHandler? ThemeChanged;

        /// <summary>Hooks window creation so every new window gets the matching caption color.</summary>
        public static void Initialize()
        {
            if (_initialized) return;
            _initialized = true;
            EventManager.RegisterClassHandler(typeof(Window), FrameworkElement.LoadedEvent,
                new RoutedEventHandler((s, _) => { if (s is Window w) ApplyCaption(w, IsDark); }));
        }

        public static void ApplyTheme(bool isDark)
        {
            var app = Application.Current;
            if (app == null) return;

            var dicts = app.Resources.MergedDictionaries;
            var current = dicts.FirstOrDefault(d => d.Contains(MarkerKey));
            bool alreadyActive = current != null && (current[MarkerKey] as string) == (isDark ? "Dark" : "Light");

            if (!alreadyActive)
            {
                var palette = new ResourceDictionary { Source = isDark ? DarkUri : LightUri };
                if (current != null)
                {
                    int index = dicts.IndexOf(current);
                    dicts[index] = palette;
                }
                else
                {
                    dicts.Insert(0, palette);
                }
            }

            IsDark = isDark;
            foreach (Window w in app.Windows)
                ApplyCaption(w, isDark);

            ThemeChanged?.Invoke(null, EventArgs.Empty);
        }

        /// <summary>Reads a color from the active palette (e.g. "PlotTextColor").</summary>
        public static System.Windows.Media.Color? GetColor(string key) =>
            Application.Current?.TryFindResource(key) as System.Windows.Media.Color?;

        // ---------- Dark title bar (best effort, ignored on older Windows) ----------

        private const int DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;   // Windows 10 1809..1909
        private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;       // Windows 10 2004+ / 11
        private const uint SWP_NOSIZE = 0x0001, SWP_NOMOVE = 0x0002, SWP_NOZORDER = 0x0004,
                           SWP_NOACTIVATE = 0x0010, SWP_FRAMECHANGED = 0x0020;

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint flags);

        private static void ApplyCaption(Window window, bool isDark)
        {
            try
            {
                var hwnd = new WindowInteropHelper(window).Handle;
                if (hwnd == IntPtr.Zero) return;

                int value = isDark ? 1 : 0;
                if (DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref value, sizeof(int)) != 0)
                    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, ref value, sizeof(int));

                // Force a non-client repaint so an already visible caption changes immediately
                SetWindowPos(hwnd, IntPtr.Zero, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            catch (Exception)
            {
                // dwmapi missing or attribute unsupported: keep the default caption
            }
        }
    }
}
