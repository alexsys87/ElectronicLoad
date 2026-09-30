using System.Globalization;
using System.Windows;

namespace Electronic_Load
{
    public partial class CoordinateWindow : Window
    {
        public double YLeftMinValue { get; private set; }
        public double YLeftMaxValue { get; private set; }
        public double YRightMinValue { get; private set; }
        public double YRightMaxValue { get; private set; }
        public double TimeMinValue { get; private set; }
        public double TimeMaxValue { get; private set; }

        public CoordinateWindow(double yLeftMin, double yLeftMax, double yRightMin, double yRightMax, double timeMin, double timeMax)
        {
            InitializeComponent();

            YLeftMin.Text = Format(yLeftMin);
            YLeftMax.Text = Format(yLeftMax);
            YRightMin.Text = Format(yRightMin);
            YRightMax.Text = Format(yRightMax);
            TimeMin.Text = Format(timeMin);
            TimeMax.Text = Format(timeMax);
        }

        private static string Format(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);

        private void OKButton_Click(object sender, RoutedEventArgs e)
        {
            // Both '.' and ',' are accepted as the decimal separator
            if (!InputParsing.TryParseDouble(YLeftMin.Text, out double yLeftMin) ||
                !InputParsing.TryParseDouble(YLeftMax.Text, out double yLeftMax) ||
                !InputParsing.TryParseDouble(YRightMin.Text, out double yRightMin) ||
                !InputParsing.TryParseDouble(YRightMax.Text, out double yRightMax) ||
                !InputParsing.TryParseDouble(TimeMin.Text, out double timeMin) ||
                !InputParsing.TryParseDouble(TimeMax.Text, out double timeMax))
            {
                MessageBox.Show("Please enter valid numeric values.", "Input Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            if (yLeftMin >= yLeftMax || yRightMin >= yRightMax || timeMin >= timeMax)
            {
                MessageBox.Show("Minimum value must be less than maximum.", "Range Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            YLeftMinValue = yLeftMin;
            YLeftMaxValue = yLeftMax;
            YRightMinValue = yRightMin;
            YRightMaxValue = yRightMax;
            TimeMinValue = timeMin;
            TimeMaxValue = timeMax;

            DialogResult = true;
        }
    }
}
