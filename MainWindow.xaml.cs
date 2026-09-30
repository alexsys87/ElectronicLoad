using System;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows;
using System.Windows.Threading;

namespace Electronic_Load
{
    public partial class MainWindow : Window
    {
        private readonly MainViewModel _vm;
        private bool _scrollPending;

        public MainWindow()
        {
            InitializeComponent();
            _vm = new MainViewModel();
            DataContext = _vm;

            // Keep the newest record visible; coalesce bursts (CSV import adds thousands of rows)
            _vm.DataRecords.CollectionChanged += OnRecordsChanged;
            Closing += OnClosing;
        }

        private void OnRecordsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.Action != NotifyCollectionChangedAction.Add || _scrollPending)
                return;

            _scrollPending = true;
            Dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() =>
            {
                _scrollPending = false;
                if (_vm.DataRecords.Count > 0)
                    DataGrid.ScrollIntoView(_vm.DataRecords[^1]);
            }));
        }

        private void OnClosing(object? sender, CancelEventArgs e)
        {
            // Switches the load off, closes the port and saves the settings
            _vm.Shutdown();
        }
    }
}
