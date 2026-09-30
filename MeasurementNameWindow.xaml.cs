using System.Windows;

namespace Electronic_Load
{
    public partial class MeasurementNameWindow : Window
    {
        public string MeasurementName { get; private set; }

        public MeasurementNameWindow(string currentName)
        {
            InitializeComponent();
            MeasurementName = currentName;
            NameTextBox.Text = currentName;
            NameTextBox.SelectAll();
        }

        private void OkButton_Click(object sender, RoutedEventArgs e)
        {
            MeasurementName = NameTextBox.Text.Trim();
            DialogResult = true;
        }
    }
}
