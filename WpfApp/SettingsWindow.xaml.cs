using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using OxyPlot;

namespace Electronic_Load
{
    public partial class SettingsWindow : Window
    {
        public List<NamedColor> AvailableColors { get; } = new()
        {
            new NamedColor("Auto (theme)", OxyColors.Automatic),
            new NamedColor("Blue", OxyColors.Blue),
            new NamedColor("Red", OxyColors.Red),
            new NamedColor("Green", OxyColors.Green),
            new NamedColor("Orange", OxyColors.Orange),
            new NamedColor("Purple", OxyColors.Purple),
            new NamedColor("Brown", OxyColors.Brown),
            new NamedColor("Black", OxyColors.Black),
            new NamedColor("Gray", OxyColors.Gray),
            new NamedColor("Cyan", OxyColors.Cyan),
            new NamedColor("Magenta", OxyColors.Magenta),
            new NamedColor("Yellow", OxyColors.Yellow),
            new NamedColor("White", OxyColors.White)
        };

        public GraphSettings Settings { get; }

        public SettingsWindow(GraphSettings settings)
        {
            InitializeComponent();
            Settings = settings;

            Init(VoltageColorCombo, settings.VoltageColor);
            Init(CurrentColorCombo, settings.CurrentColor);
            Init(TemperatureColorCombo, settings.TemperatureColor);
            Init(PowerColorCombo, settings.PowerColor);
            Init(LegendBackgroundCombo, settings.LegendBackground);
            Init(LegendBorderCombo, settings.LegendBorder);
        }

        private void Init(ComboBox combo, OxyColor color)
        {
            var items = new List<NamedColor>(AvailableColors);
            int index = items.FindIndex(c => c.Color == color);
            if (index < 0)
            {
                // Keep a custom color (e.g. from an older settings file) selectable instead of replacing it
                items.Add(new NamedColor(GraphSettings.FormatColor(color), color));
                index = items.Count - 1;
            }
            combo.ItemsSource = items;
            combo.SelectedIndex = index;
        }

        private static OxyColor Selected(ComboBox combo) =>
            combo.SelectedItem is NamedColor nc ? nc.Color : OxyColors.Automatic;

        private void OkButton_Click(object sender, RoutedEventArgs e)
        {
            Settings.VoltageColor = Selected(VoltageColorCombo);
            Settings.CurrentColor = Selected(CurrentColorCombo);
            Settings.TemperatureColor = Selected(TemperatureColorCombo);
            Settings.PowerColor = Selected(PowerColorCombo);
            Settings.LegendBackground = Selected(LegendBackgroundCombo);
            Settings.LegendBorder = Selected(LegendBorderCombo);

            DialogResult = true;
        }
    }

    /// <summary>Item of the color combo boxes.</summary>
    public class NamedColor
    {
        public string Name { get; }
        public OxyColor Color { get; }

        public NamedColor(string name, OxyColor color)
        {
            Name = name;
            Color = color;
        }

        public override string ToString() => Name;
    }
}
