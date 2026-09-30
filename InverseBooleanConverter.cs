using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;
using OxyPlot;

namespace Electronic_Load
{
    public class InverseBooleanConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture) =>
            value is bool b ? !b : Binding.DoNothing;

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
            value is bool b ? !b : Binding.DoNothing;
    }

    /// <summary>OxyColor -> Brush for color swatches; "automatic" is shown as a hatched gray.</summary>
    public class OxyColorToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is not OxyColor c)
                return Brushes.Transparent;
            if (c == OxyColors.Automatic)
                return Brushes.Gray;
            var brush = new SolidColorBrush(Color.FromArgb(c.A, c.R, c.G, c.B));
            brush.Freeze();
            return brush;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) =>
            throw new NotSupportedException();
    }
}
