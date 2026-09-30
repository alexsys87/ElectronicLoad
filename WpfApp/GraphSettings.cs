using System;
using System.ComponentModel;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;
using OxyPlot;

namespace Electronic_Load
{
    /// <summary>
    /// User-selected graph colors. <see cref="OxyColors.Automatic"/> means "follow the current theme".
    /// </summary>
    public class GraphSettings : INotifyPropertyChanged
    {
        private OxyColor _voltageColor = OxyColors.Automatic;
        private OxyColor _currentColor = OxyColors.Automatic;
        private OxyColor _temperatureColor = OxyColors.Automatic;
        private OxyColor _powerColor = OxyColors.Automatic;
        private OxyColor _legendBackground = OxyColors.Automatic;
        private OxyColor _legendBorder = OxyColors.Automatic;

        public OxyColor VoltageColor { get => _voltageColor; set => Set(ref _voltageColor, value); }
        public OxyColor CurrentColor { get => _currentColor; set => Set(ref _currentColor, value); }
        public OxyColor TemperatureColor { get => _temperatureColor; set => Set(ref _temperatureColor, value); }
        public OxyColor PowerColor { get => _powerColor; set => Set(ref _powerColor, value); }
        public OxyColor LegendBackground { get => _legendBackground; set => Set(ref _legendBackground, value); }
        public OxyColor LegendBorder { get => _legendBorder; set => Set(ref _legendBorder, value); }

        public GraphSettings Clone() => (GraphSettings)MemberwiseClone();

        public event PropertyChangedEventHandler? PropertyChanged;

        private void Set(ref OxyColor field, OxyColor value, [CallerMemberName] string? name = null)
        {
            if (field == value) return;
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }

        // ---------- Persistence helpers ----------

        /// <summary>
        /// Parses a stored color: "auto", a named color ("Blue", as written by the previous version),
        /// "#AARRGGBB"/"#RRGGBB" or "r,g,b". OxyColor.Parse alone does not understand color names.
        /// </summary>
        public static OxyColor ParseColor(string? text, OxyColor fallback)
        {
            if (string.IsNullOrWhiteSpace(text))
                return fallback;
            text = text.Trim();

            if (text.Equals("auto", StringComparison.OrdinalIgnoreCase) ||
                text.Equals("automatic", StringComparison.OrdinalIgnoreCase))
                return OxyColors.Automatic;

            var field = typeof(OxyColors).GetField(text,
                BindingFlags.Public | BindingFlags.Static | BindingFlags.IgnoreCase);
            if (field?.GetValue(null) is OxyColor named)
                return named;

            try
            {
                return OxyColor.Parse(text);
            }
            catch (Exception)
            {
                return fallback;
            }
        }

        public static string FormatColor(OxyColor color) =>
            color == OxyColors.Automatic
                ? "auto"
                : string.Format(CultureInfo.InvariantCulture, "#{0:X2}{1:X2}{2:X2}{3:X2}", color.A, color.R, color.G, color.B);
    }
}
