using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using Microsoft.Win32;
using OxyPlot;
using OxyPlot.Wpf; // для PngExporter
using OxyPlot.Axes;
using OxyPlot.Legends;
using OxyPlot.Series;
using AppSettings = Electronic_Load.Properties.Settings;

namespace Electronic_Load
{
    public sealed class MainViewModel : INotifyPropertyChanged
    {
        // ---------- Polling ----------
        private const int PollPeriodMs = 1000;        // one full snapshot = 7 queries (~150 ms at 9600 baud)
        private const int SetpointReadEveryN = 5;     // read device setpoints every N polls
        private const int FailuresBeforeWarning = 3;

        // Series indices in PlotModel.Series
        private const int SerVoltage = 0, SerCurrent = 1, SerTemperature = 2, SerPower = 3;

        private SerialPortTransport? _transport;
        private PX100Protocol? _protocol;
        private CancellationTokenSource? _pollCts;
        private Task? _pollTask;

        private bool _isConnected;
        private bool _isRunning;          // logging session active (load switched on by this program)
        private bool _isBusy;             // a user command is talking to the device
        private DateTime _sessionStart;   // t = 0 of the graph
        private DateTime _runStartedAt;   // snapshots older than this are ignored for the running state
        private bool _isLoading;
        private bool _logImported;        // DataRecords came from a CSV file, not from this session

        // Data extents for auto scale
        private double _minX = double.NaN, _maxX = double.NaN;
        private double _minLeft = double.NaN, _maxLeft = double.NaN;
        private double _minRight = double.NaN, _maxRight = double.NaN;
        private double _minTemp = double.NaN, _maxTemp = double.NaN;

        public MainViewModel()
        {
            _isLoading = true;

            AvailablePorts = new ObservableCollection<string>();
            DataRecords = new ObservableCollection<DataRecord>();
            PlotModel = CreatePlotModel();

            // Commands
            ConnectCommand = new RelayCommand(async _ => await ToggleConnectionAsync(), _ => !_isBusy);
            StartStopCommand = new RelayCommand(async _ => await StartStopAsync(), _ => _isConnected && !_isBusy);
            ApplySettingsCommand = new RelayCommand(async _ => await ApplySettingsAsync(), _ => _isConnected && !_isBusy);
            ClearDataCommand = new RelayCommand(async _ => await ClearDataAsync(), _ => !_isBusy);
            ClearGraphCommand = new RelayCommand(_ => ClearLog());
            ScanComPortsCommand = new RelayCommand(_ => ScanPorts(showResult: true), _ => !_isConnected);
            ExportGraphCommand = new RelayCommand(_ => ExportCsv(), _ => DataRecords.Count > 0);
            ImportGraphCommand = new RelayCommand(_ => ImportCsv(), _ => !_isRunning);
            ExportPngGraphCommand = new RelayCommand(_ => ExportPng());
            SettingsCommand = new RelayCommand(_ => OpenGraphSettings());
            SetCoordinateCommand = new RelayCommand(_ => SetCoordinate());
            SetMeasurementNameCommand = new RelayCommand(_ => SetMeasurementName());
            AboutCommand = new RelayCommand(_ => ShowAbout());
            ExitCommand = new RelayCommand(_ => Application.Current.MainWindow?.Close());
            SetThemeCommand = new RelayCommand(p => IsDarkTheme = string.Equals(p?.ToString(), "Dark", StringComparison.OrdinalIgnoreCase));
            ToggleThemeCommand = new RelayCommand(_ => IsDarkTheme = !IsDarkTheme);

            LoadSettings();
            ScanPorts(showResult: false);

            ThemeManager.ThemeChanged += (_, _) => ApplyPlotStyle();
            ApplyPlotStyle();
            ApplyAxisRanges();

            var clock = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            clock.Tick += (_, _) => CurrentTime = DateTime.Now;
            clock.Start();
            CurrentTime = DateTime.Now;

            _isLoading = false;
        }

        // =====================================================================
        // Commands
        // =====================================================================
        public ICommand ConnectCommand { get; }
        public ICommand StartStopCommand { get; }
        public ICommand ApplySettingsCommand { get; }
        public ICommand ClearDataCommand { get; }
        public ICommand ClearGraphCommand { get; }
        public ICommand ScanComPortsCommand { get; }
        public ICommand ExportGraphCommand { get; }
        public ICommand ImportGraphCommand { get; }
        public ICommand ExportPngGraphCommand { get; }
        public ICommand SettingsCommand { get; }
        public ICommand SetCoordinateCommand { get; }
        public ICommand SetMeasurementNameCommand { get; }
        public ICommand AboutCommand { get; }
        public ICommand ExitCommand { get; }
        public ICommand SetThemeCommand { get; }
        public ICommand ToggleThemeCommand { get; }

        // =====================================================================
        // Bindable properties
        // =====================================================================
        public ObservableCollection<string> AvailablePorts { get; }
        public ObservableCollection<DataRecord> DataRecords { get; }
        public PlotModel PlotModel { get; }

        private string _selectedPort = string.Empty;
        public string SelectedPort
        {
            get => _selectedPort;
            set { if (Set(ref _selectedPort, value ?? string.Empty)) SaveSettings(); }
        }

        private int _baudRate = 9600;
        public int BaudRate
        {
            get => _baudRate;
            set { if (Set(ref _baudRate, value)) SaveSettings(); }
        }

        // ---- Live readings ----
        private double _voltage, _current, _capacity, _energy, _temperature;
        public double Voltage { get => _voltage; private set { if (Set(ref _voltage, value)) OnPropertyChanged(nameof(Power)); } }
        public double Current { get => _current; private set { if (Set(ref _current, value)) OnPropertyChanged(nameof(Power)); } }
        public double Power => Voltage * Current;
        public double Capacity { get => _capacity; private set => Set(ref _capacity, value); }
        public double Energy { get => _energy; private set => Set(ref _energy, value); }
        public double Temperature { get => _temperature; private set => Set(ref _temperature, value); }

        private string _deviceTimeText = "--:--:--";
        public string DeviceTimeText { get => _deviceTimeText; private set => Set(ref _deviceTimeText, value); }

        private bool _isLoadOn;
        public bool IsLoadOn
        {
            get => _isLoadOn;
            private set { if (Set(ref _isLoadOn, value)) OnPropertyChanged(nameof(LoadStateText)); }
        }
        public string LoadStateText => !_isConnected ? "—" : IsLoadOn ? "LOAD ON" : "LOAD OFF";

        private string _deviceSetpointsText = "—";
        public string DeviceSetpointsText { get => _deviceSetpointsText; private set => Set(ref _deviceSetpointsText, value); }

        // ---- Setpoints entered by the user (text, parsed on Apply/Start; accepts '.' or ',') ----
        private string _setCurrentText = "1";
        public string SetCurrentText { get => _setCurrentText; set => Set(ref _setCurrentText, value); }

        private string _cutoffVoltageText = "3";
        public string CutoffVoltageText { get => _cutoffVoltageText; set => Set(ref _cutoffVoltageText, value); }

        private string _timerText = "00:00:00";
        public string TimerText { get => _timerText; set => Set(ref _timerText, value); }

        // ---- State ----
        public bool IsConnected => _isConnected;
        public bool IsRunning => _isRunning;
        public bool CanEditPort => !_isConnected && !_isBusy;
        public string ConnectButtonText => _isConnected ? "Disconnect" : "Connect";
        public string StartStopButtonText => _isRunning ? "Stop" : "Start";

        private string _connectionStatus = "Disconnected";
        public string ConnectionStatus { get => _connectionStatus; private set => Set(ref _connectionStatus, value); }

        private string _lastEvent = string.Empty;
        public string LastEvent { get => _lastEvent; private set => Set(ref _lastEvent, value); }

        private DateTime _currentTime;
        public DateTime CurrentTime { get => _currentTime; private set => Set(ref _currentTime, value); }

        private string _measurementName = "Untitled";
        public string MeasurementName
        {
            get => _measurementName;
            set
            {
                if (!Set(ref _measurementName, value ?? string.Empty)) return;
                PlotModel.Title = _measurementName;
                PlotModel.InvalidatePlot(false);
                SaveSettings();
            }
        }

        private bool _autoScaleEnabled = true;
        public bool AutoScaleEnabled
        {
            get => _autoScaleEnabled;
            set
            {
                if (!Set(ref _autoScaleEnabled, value)) return;
                ApplyAxisRanges();
                SaveSettings();
            }
        }

        private bool _isDarkTheme;
        public bool IsDarkTheme
        {
            get => _isDarkTheme;
            set
            {
                if (!Set(ref _isDarkTheme, value)) return;
                ThemeManager.ApplyTheme(value);   // raises ThemeChanged -> ApplyPlotStyle()
                OnPropertyChanged(nameof(ThemeToggleGlyph));
                SaveSettings();
            }
        }

        /// <summary>Icon of the header toggle: shows the theme you would switch to.</summary>
        public string ThemeToggleGlyph => IsDarkTheme ? "☀" : "🌙";

        private GraphSettings _graphSettings = new();
        public GraphSettings GraphSettings
        {
            get => _graphSettings;
            private set
            {
                _graphSettings = value;
                OnPropertyChanged();
                ApplyPlotStyle();
            }
        }

        // Manual axis ranges (used when auto scale is off)
        private double _yLeftMin, _yLeftMax = 10, _yRightMin, _yRightMax = 30, _xMin, _xMax = 30;

        // =====================================================================
        // Connection
        // =====================================================================
        private void ScanPorts(bool showResult)
        {
            string previous = SelectedPort;
            var ports = SerialPort.GetPortNames().Distinct().OrderBy(p => p.Length).ThenBy(p => p).ToList();

            AvailablePorts.Clear();
            foreach (var p in ports)
                AvailablePorts.Add(p);

            // Keep the saved/previous port when it still exists.
            // Force a notification: clearing the list may have reset the ComboBox selection.
            _selectedPort = string.Empty;
            SelectedPort = ports.Contains(previous) ? previous : ports.FirstOrDefault() ?? string.Empty;

            if (showResult)
                MessageBox.Show($"Found {ports.Count} COM port(s).", "Scan", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private async Task ToggleConnectionAsync()
        {
            if (_isConnected)
                await DisconnectAsync(switchLoadOff: true);
            else
                await ConnectAsync();
        }

        private async Task ConnectAsync()
        {
            if (string.IsNullOrEmpty(SelectedPort))
            {
                MessageBox.Show("Select a COM port first.", "Connect", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            SetBusy(true);
            ConnectionStatus = $"Connecting to {SelectedPort}...";
            var transport = new SerialPortTransport(SelectedPort, BaudRate);
            try
            {
                var protocol = new PX100Protocol(transport);
                Px100Setpoints setpoints = await Task.Run(() =>
                {
                    transport.Open();
                    if (!protocol.Probe())
                        throw new Px100Exception("The device does not answer. Check the port, wiring (TX/RX crossed) and 9600 baud.");
                    return protocol.ReadSetpoints();
                });

                _transport = transport;
                _protocol = protocol;
                _isConnected = true;
                ConnectionStatus = $"Connected to {SelectedPort}";
                LastEvent = string.Empty;
                ShowDeviceSetpoints(setpoints);
                RaiseStateChanged();
                StartPolling();
            }
            catch (Exception ex)
            {
                transport.Dispose();
                ConnectionStatus = "Disconnected";
                MessageBox.Show($"Connection error: {ex.Message}", "Connect", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                SetBusy(false);
            }
        }

        private async Task DisconnectAsync(bool switchLoadOff)
        {
            SetBusy(true);
            try
            {
                await StopPollingAsync();

                var protocol = _protocol;
                if (switchLoadOff && protocol != null && (IsLoadOn || _isRunning))
                {
                    try { await Task.Run(() => protocol.SetOutput(false)); }
                    catch (Exception ex) { Debug.WriteLine($"Load OFF on disconnect failed: {ex.Message}"); }
                }
            }
            finally
            {
                CloseLink("Disconnected");
                SetBusy(false);
            }
        }

        /// <summary>Releases the port and resets the connection state (UI thread).</summary>
        private void CloseLink(string status)
        {
            _transport?.Dispose();
            _transport = null;
            _protocol = null;
            _isConnected = false;
            if (_isRunning)
            {
                _isRunning = false;
                LastEvent = "Logging stopped: connection closed";
            }
            IsLoadOn = false;
            DeviceTimeText = "--:--:--";
            DeviceSetpointsText = "—";
            ConnectionStatus = status;
            RaiseStateChanged();
        }

        /// <summary>
        /// Called from MainWindow.Closing. Blocks briefly: stops polling, switches the load off
        /// (same as the reference software does on exit) and closes the port.
        /// </summary>
        public void Shutdown()
        {
            // Do not Wait() for the poll task here: its continuations need this (UI) thread.
            // The protocol lock serializes the OFF command with any transaction still in flight.
            _pollCts?.Cancel();

            var protocol = _protocol;
            if (protocol != null)
            {
                try
                {
                    protocol.TransactionAttempts = 2;
                    protocol.VerifyAttempts = 1;
                    protocol.SetOutput(false);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Load OFF on exit failed: {ex.Message}");
                }
            }
            _transport?.Dispose();
            _transport = null;
            _protocol = null;
            SaveSettings(force: true);
        }

        // =====================================================================
        // Polling (runs on the UI thread, blocking I/O on the thread pool)
        // =====================================================================
        private void StartPolling()
        {
            _pollCts = new CancellationTokenSource();
            _pollTask = PollLoopAsync(_pollCts.Token);
        }

        private async Task StopPollingAsync()
        {
            if (_pollCts == null) return;
            _pollCts.Cancel();
            try { if (_pollTask != null) await _pollTask; }
            catch (OperationCanceledException) { }
            _pollCts.Dispose();
            _pollCts = null;
            _pollTask = null;
        }

        private async Task PollLoopAsync(CancellationToken ct)
        {
            int cycle = 0;
            int failures = 0;

            while (!ct.IsCancellationRequested)
            {
                var sw = Stopwatch.StartNew();
                var protocol = _protocol;
                if (protocol == null) break;

                try
                {
                    bool readSetpoints = cycle % SetpointReadEveryN == SetpointReadEveryN - 1;
                    var (snapshot, setpoints) = await Task.Run(() =>
                        (protocol.ReadSnapshot(), readSetpoints ? protocol.ReadSetpoints() : null), ct);

                    if (ct.IsCancellationRequested) break;
                    if (failures >= FailuresBeforeWarning)
                        ConnectionStatus = $"Connected to {SelectedPort}";
                    failures = 0;

                    ApplySnapshot(snapshot);
                    if (setpoints != null)
                        ShowDeviceSetpoints(setpoints);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Px100Exception ex)
                {
                    failures++;
                    Debug.WriteLine($"Poll error: {ex.Message}");
                    if (failures >= FailuresBeforeWarning)
                        ConnectionStatus = $"{SelectedPort}: no response from the device";
                }
                catch (Exception ex) when (ex is IOException or InvalidOperationException or UnauthorizedAccessException)
                {
                    // Port vanished (USB adapter unplugged) or was closed under us
                    if (ct.IsCancellationRequested) break;
                    CloseLink("Disconnected (port lost)");
                    MessageBox.Show($"Serial port error: {ex.Message}", "Connection lost", MessageBoxButton.OK, MessageBoxImage.Error);
                    break;
                }

                cycle++;
                int wait = PollPeriodMs - (int)sw.ElapsedMilliseconds;
                try { await Task.Delay(Math.Max(wait, 50), ct); }
                catch (OperationCanceledException) { break; }
            }
        }

        private void ApplySnapshot(Px100Snapshot s)
        {
            Voltage = s.Voltage;
            Current = s.Current;
            Capacity = s.CapacityMAh;
            Energy = s.EnergyMWh;
            Temperature = s.Temperature;
            DeviceTimeText = InputParsing.FormatDuration(s.Elapsed);
            IsLoadOn = s.IsOn;

            if (!_isRunning || s.Timestamp < _runStartedAt)
                return;

            AddRecord(new DataRecord
            {
                Time = s.Timestamp,
                Elapsed = (s.Timestamp - _sessionStart).TotalSeconds,
                Voltage = s.Voltage,
                Current = s.Current,
                Power = s.Voltage * s.Current,
                Capacity = s.CapacityMAh,
                Energy = s.EnergyMWh,
                Temperature = s.Temperature
            });

            if (!s.IsOn)
            {
                // The load switched itself off: cutoff voltage, timer or a protection
                _isRunning = false;
                LastEvent = $"Load switched off by the device at {s.Timestamp:HH:mm:ss} " +
                            $"({s.Voltage:F2} V, {s.CapacityMAh:F0} mAh)";
                RaiseStateChanged();
            }
        }

        private void ShowDeviceSetpoints(Px100Setpoints sp)
        {
            string timer = sp.Timer == TimeSpan.Zero ? "off" : InputParsing.FormatDuration(sp.Timer);
            DeviceSetpointsText = $"Device: {sp.Current:F2} A, cutoff {sp.CutoffVoltage:F2} V, timer {timer}";
        }

        // =====================================================================
        // Load control
        // =====================================================================
        private bool TryReadSetpoints(out double current, out double cutoff, out TimeSpan timer)
        {
            timer = TimeSpan.Zero;
            cutoff = 0;
            string? error = null;

            if (!InputParsing.TryParseDouble(SetCurrentText, out current) || current < 0 || current > PX100Protocol.MaxFixedPointValue)
                error = $"Current must be a number within 0 .. {PX100Protocol.MaxFixedPointValue:0.00} A.";
            else if (!InputParsing.TryParseDouble(CutoffVoltageText, out cutoff) || cutoff < 0 || cutoff > PX100Protocol.MaxFixedPointValue)
                error = $"Cutoff voltage must be a number within 0 .. {PX100Protocol.MaxFixedPointValue:0.00} V.";
            else if (!InputParsing.TryParseDuration(TimerText, out timer) || timer > PX100Protocol.MaxTimer)
                error = "Timer must be hh:mm:ss within 00:00:00 .. 18:12:15 (00:00:00 = off).";

            if (error != null)
            {
                MessageBox.Show(error, "Invalid setting", MessageBoxButton.OK, MessageBoxImage.Warning);
                return false;
            }

            // Show the values exactly as they will be sent (0.01 resolution)
            current = Math.Round(current, 2, MidpointRounding.AwayFromZero);
            cutoff = Math.Round(cutoff, 2, MidpointRounding.AwayFromZero);
            SetCurrentText = InputParsing.FormatNumber(current);
            CutoffVoltageText = InputParsing.FormatNumber(cutoff);
            TimerText = InputParsing.FormatDuration(timer);
            return true;
        }

        private async Task<bool> SendSetpointsAsync(PX100Protocol protocol)
        {
            if (!TryReadSetpoints(out double current, out double cutoff, out TimeSpan timer))
                return false;

            var sp = await Task.Run(() =>
            {
                protocol.SetCurrent(current);
                protocol.SetCutoffVoltage(cutoff);
                protocol.SetTimer(timer);
                return protocol.ReadSetpoints();
            });
            ShowDeviceSetpoints(sp);
            SaveSettings();
            return true;
        }

        private async Task ApplySettingsAsync()
        {
            var protocol = _protocol;
            if (protocol == null) return;

            SetBusy(true);
            try
            {
                if (await SendSetpointsAsync(protocol))
                    LastEvent = "Settings applied";
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to apply settings: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                SetBusy(false);
            }
        }

        private async Task StartStopAsync()
        {
            var protocol = _protocol;
            if (protocol == null) return;

            SetBusy(true);
            try
            {
                if (_isRunning)
                {
                    _isRunning = false;   // stop logging first so no sample is taken after OFF
                    RaiseStateChanged();
                    await Task.Run(() => protocol.SetOutput(false));
                    LastEvent = $"Stopped at {DateTime.Now:HH:mm:ss}";
                }
                else
                {
                    if (!await SendSetpointsAsync(protocol))
                        return;
                    await Task.Run(() => protocol.SetOutput(true));

                    // A new session starts at t = 0; after Stop the log continues on the same time axis.
                    // Imported data has its own time base, so it is replaced by the new session.
                    if (_logImported)
                        ClearLog();
                    if (DataRecords.Count == 0)
                        _sessionStart = DateTime.Now;
                    _runStartedAt = DateTime.Now;
                    _isRunning = true;
                    IsLoadOn = true;
                    LastEvent = $"Started at {DateTime.Now:HH:mm:ss}";
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Device error: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                RaiseStateChanged();
                SetBusy(false);
            }
        }

        /// <summary>Clears the log and the device mAh/mWh/time counters.</summary>
        private async Task ClearDataAsync()
        {
            ClearLog();
            var protocol = _protocol;
            if (protocol == null) return;

            SetBusy(true);
            try
            {
                await Task.Run(protocol.ResetCounters);
                Capacity = 0;
                Energy = 0;
                LastEvent = "Counters reset";
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to reset counters: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                SetBusy(false);
            }
        }

        // =====================================================================
        // Log and graph
        // =====================================================================
        private PlotModel CreatePlotModel()
        {
            var model = new PlotModel { Title = MeasurementName };
            model.Legends.Add(new Legend
            {
                LegendPosition = LegendPosition.TopRight,
                LegendPlacement = LegendPlacement.Inside,
                LegendOrientation = LegendOrientation.Vertical
            });

            model.Axes.Add(CreateAxis(AxisPosition.Left, "Voltage (V) / Current (A)", "LeftAxis"));
            model.Axes.Add(CreateAxis(AxisPosition.Right, "Power (W)", "RightAxis"));
            // Temperature has its own scale: sharing the voltage axis squeezes the discharge curve flat
            var tempAxis = CreateAxis(AxisPosition.Right, "Temp (°C)", "TempAxis");
            tempAxis.PositionTier = 1;
            model.Axes.Add(tempAxis);
            model.Axes.Add(CreateAxis(AxisPosition.Bottom, "Time (s)", "BottomAxis"));

            model.Series.Add(CreateSeries("Voltage", "LeftAxis"));
            model.Series.Add(CreateSeries("Current", "LeftAxis"));
            model.Series.Add(CreateSeries("Temperature", "TempAxis"));
            model.Series.Add(CreateSeries("Power", "RightAxis"));
            return model;
        }

        private static LinearAxis CreateAxis(AxisPosition position, string title, string key) => new()
        {
            Position = position,
            Title = title,
            Key = key,
            MajorGridlineStyle = position == AxisPosition.Right ? LineStyle.None : LineStyle.Solid,
            MinorGridlineStyle = position == AxisPosition.Right ? LineStyle.None : LineStyle.Dot,
            MajorGridlineThickness = 1,
            MinorGridlineThickness = 0.5
        };

        private static LineSeries CreateSeries(string title, string yAxisKey) => new()
        {
            Title = title,
            StrokeThickness = 2,
            MarkerType = MarkerType.None,
            YAxisKey = yAxisKey,
            XAxisKey = "BottomAxis"
        };

        private LineSeries SeriesAt(int index) => (LineSeries)PlotModel.Series[index];

        private Axis AxisByKey(string key) => PlotModel.Axes.First(a => a.Key == key);

        private void AddRecord(DataRecord r)
        {
            DataRecords.Add(r);
            AddPoints(r);
            if (AutoScaleEnabled) ApplyAxisRanges();
            PlotModel.InvalidatePlot(true);
        }

        private void AddPoints(DataRecord r)
        {
            SeriesAt(SerVoltage).Points.Add(new DataPoint(r.Elapsed, r.Voltage));
            SeriesAt(SerCurrent).Points.Add(new DataPoint(r.Elapsed, r.Current));
            SeriesAt(SerTemperature).Points.Add(new DataPoint(r.Elapsed, r.Temperature));
            SeriesAt(SerPower).Points.Add(new DataPoint(r.Elapsed, r.Power));

            _minX = Min(_minX, r.Elapsed); _maxX = Max(_maxX, r.Elapsed);
            // Both left-axis series count, not only the voltage
            _minLeft = Min(_minLeft, Math.Min(r.Voltage, r.Current));
            _maxLeft = Max(_maxLeft, Math.Max(r.Voltage, r.Current));
            _minRight = Min(_minRight, r.Power); _maxRight = Max(_maxRight, r.Power);
            _minTemp = Min(_minTemp, r.Temperature); _maxTemp = Max(_maxTemp, r.Temperature);
        }

        private static double Min(double acc, double v) => double.IsNaN(acc) ? v : Math.Min(acc, v);
        private static double Max(double acc, double v) => double.IsNaN(acc) ? v : Math.Max(acc, v);

        private void ClearLog()
        {
            DataRecords.Clear();
            foreach (var s in PlotModel.Series.OfType<LineSeries>())
                s.Points.Clear();
            _minX = _maxX = _minLeft = _maxLeft = _minRight = _maxRight = _minTemp = _maxTemp = double.NaN;
            _sessionStart = DateTime.Now;
            _logImported = false;
            ApplyAxisRanges();
            PlotModel.InvalidatePlot(true);
            CommandManager.InvalidateRequerySuggested();
        }

        /// <summary>Applies auto-scaled (data extents) or manual axis ranges.</summary>
        private void ApplyAxisRanges()
        {
            var left = AxisByKey("LeftAxis");
            var right = AxisByKey("RightAxis");
            var bottom = AxisByKey("BottomAxis");

            // The temperature axis always follows the data (no manual range for it)
            SetRange(AxisByKey("TempAxis"), double.IsNaN(_maxTemp)
                ? (0, 50)
                : (Math.Floor(_minTemp) - 2, Math.Ceiling(_maxTemp) + 2));

            if (AutoScaleEnabled && !double.IsNaN(_maxX))
            {
                SetRange(left, PadRange(Math.Min(0, _minLeft), _maxLeft));
                SetRange(right, PadRange(Math.Min(0, _minRight), _maxRight));
                double x0 = _minX, x1 = Math.Max(_maxX, _minX + 10);
                SetRange(bottom, (x0, x1 + (x1 - x0) * 0.02));
            }
            else
            {
                SetRange(left, (_yLeftMin, _yLeftMax));
                SetRange(right, (_yRightMin, _yRightMax));
                SetRange(bottom, (_xMin, _xMax));
            }
            PlotModel.InvalidatePlot(false);
        }

        private static (double, double) PadRange(double min, double max)
        {
            double range = max - min;
            if (range < 1e-9) range = 1;
            return (min < 0 ? min - range * 0.05 : min, min + range * 1.05);
        }

        private static void SetRange(Axis axis, (double Min, double Max) r)
        {
            axis.Minimum = r.Min;
            axis.Maximum = r.Max;
            axis.Reset();   // drop mouse zoom/pan so the new range is visible
        }

        private void ApplyPlotStyle()
        {
            OxyColor Theme(string key, OxyColor fallback)
            {
                var c = ThemeManager.GetColor(key);
                return c.HasValue ? OxyColor.FromArgb(c.Value.A, c.Value.R, c.Value.G, c.Value.B) : fallback;
            }
            OxyColor Pick(OxyColor user, string key, OxyColor fallback) =>
                user == OxyColors.Automatic ? Theme(key, fallback) : user;

            var text = Theme("PlotTextColor", OxyColors.Black);
            var axisLine = Theme("PlotAxisColor", OxyColors.Gray);
            var major = Theme("PlotMajorGridColor", OxyColors.LightGray);
            var minor = Theme("PlotMinorGridColor", OxyColors.WhiteSmoke);

            PlotModel.Background = Theme("PlotBackgroundColor", OxyColors.White);   // also used by PNG export
            PlotModel.TextColor = text;
            PlotModel.TitleColor = text;
            PlotModel.PlotAreaBorderColor = axisLine;

            foreach (var axis in PlotModel.Axes)
            {
                axis.TextColor = text;
                axis.TitleColor = text;
                axis.AxislineColor = axisLine;
                axis.TicklineColor = axisLine;
                axis.MinorTicklineColor = axisLine;
                axis.MajorGridlineColor = major;
                axis.MinorGridlineColor = minor;
            }

            var gs = GraphSettings;
            SeriesAt(SerVoltage).Color = Pick(gs.VoltageColor, "VoltageColor", OxyColors.Blue);
            SeriesAt(SerCurrent).Color = Pick(gs.CurrentColor, "CurrentColor", OxyColors.Green);
            SeriesAt(SerTemperature).Color = Pick(gs.TemperatureColor, "TemperatureColor", OxyColors.Orange);
            SeriesAt(SerPower).Color = Pick(gs.PowerColor, "PowerColor", OxyColors.Red);

            var legend = PlotModel.Legends[0];
            legend.LegendTextColor = text;
            legend.LegendTitleColor = text;
            legend.LegendBackground = Pick(gs.LegendBackground, "PlotLegendBackgroundColor", OxyColor.FromAColor(200, OxyColors.White));
            legend.LegendBorder = Pick(gs.LegendBorder, "PlotLegendBorderColor", OxyColors.Black);

            PlotModel.InvalidatePlot(false);
        }

        // =====================================================================
        // Files
        // =====================================================================
        private void ExportCsv()
        {
            string safeName = string.IsNullOrWhiteSpace(MeasurementName)
                ? "graph_data"
                : MeasurementName.Replace(" ", "_").ReplaceInvalidFileNameChars();

            var dialog = new SaveFileDialog
            {
                Filter = "CSV files (*.csv)|*.csv|All files (*.*)|*.*",
                DefaultExt = "csv",
                FileName = $"{safeName}_{DateTime.Now:yyyyMMdd_HHmmss}.csv"
            };
            if (dialog.ShowDialog() != true) return;

            try
            {
                using var writer = new StreamWriter(dialog.FileName);
                DataCsv.Write(writer, DataRecords);
                MessageBox.Show($"{DataRecords.Count} records exported to {dialog.FileName}", "Export",
                    MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Export failed: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void ImportCsv()
        {
            var dialog = new OpenFileDialog
            {
                Filter = "CSV files (*.csv)|*.csv|All files (*.*)|*.*",
                DefaultExt = "csv"
            };
            if (dialog.ShowDialog() != true) return;

            try
            {
                List<DataRecord> records;
                int skipped;
                using (var reader = new StreamReader(dialog.FileName))
                    records = DataCsv.Read(reader, out skipped);

                if (records.Count == 0)
                {
                    MessageBox.Show("No data rows found in the file.", "Import", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                ClearLog();
                foreach (var r in records)
                {
                    DataRecords.Add(r);
                    AddPoints(r);
                }
                _logImported = true;
                ApplyAxisRanges();
                PlotModel.InvalidatePlot(true);

                string note = skipped > 0 ? $"\n{skipped} malformed line(s) skipped." : string.Empty;
                MessageBox.Show($"{records.Count} records imported.{note}", "Import", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Import failed: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void ExportPng()
        {
            var dialog = new SaveFileDialog
            {
                Filter = "PNG files (*.png)|*.png|All files (*.*)|*.*",
                DefaultExt = "png",
                FileName = "graph.png"
            };
            if (dialog.ShowDialog() != true) return;

            try
            {
                var exporter = new OxyPlot.Wpf.PngExporter { Width = 1200, Height = 800 };
                exporter.ExportToFile(PlotModel, dialog.FileName);
                MessageBox.Show($"Graph exported to {dialog.FileName}", "Export", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Export failed: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        // =====================================================================
        // Dialogs
        // =====================================================================
        private void SetCoordinate()
        {
            // Pre-fill with what is currently visible
            (double, double) Visible(string key, double fallbackMin, double fallbackMax)
            {
                var a = AxisByKey(key);
                return double.IsNaN(a.ActualMinimum) || double.IsNaN(a.ActualMaximum)
                    ? (fallbackMin, fallbackMax)
                    : (a.ActualMinimum, a.ActualMaximum);
            }
            var (l0, l1) = Visible("LeftAxis", _yLeftMin, _yLeftMax);
            var (r0, r1) = Visible("RightAxis", _yRightMin, _yRightMax);
            var (x0, x1) = Visible("BottomAxis", _xMin, _xMax);

            var window = new CoordinateWindow(l0, l1, r0, r1, x0, x1) { Owner = Application.Current.MainWindow };
            if (window.ShowDialog() != true) return;

            _yLeftMin = window.YLeftMinValue; _yLeftMax = window.YLeftMaxValue;
            _yRightMin = window.YRightMinValue; _yRightMax = window.YRightMaxValue;
            _xMin = window.TimeMinValue; _xMax = window.TimeMaxValue;

            // A manual range makes no sense with auto scale on
            if (AutoScaleEnabled)
                AutoScaleEnabled = false;   // applies ranges and saves
            else
            {
                ApplyAxisRanges();
                SaveSettings();
            }
        }

        private void OpenGraphSettings()
        {
            var copy = GraphSettings.Clone();
            var window = new SettingsWindow(copy) { Owner = Application.Current.MainWindow };
            if (window.ShowDialog() != true) return;

            GraphSettings = copy;
            SaveSettings();
        }

        private void SetMeasurementName()
        {
            var window = new MeasurementNameWindow(MeasurementName) { Owner = Application.Current.MainWindow };
            if (window.ShowDialog() == true)
                MeasurementName = window.MeasurementName;
        }

        private static void ShowAbout()
        {
            var about = new AboutWindow { Owner = Application.Current.MainWindow };
            about.ShowDialog();
        }

        // =====================================================================
        // Settings
        // =====================================================================
        private void LoadSettings()
        {
            var s = AppSettings.Default;
            _selectedPort = s.SelectedPort ?? string.Empty;
            _baudRate = s.BaudRate > 0 ? s.BaudRate : 9600;
            _setCurrentText = InputParsing.FormatNumber(s.SetCurrent);
            _cutoffVoltageText = InputParsing.FormatNumber(s.CutoffVoltage);
            _timerText = InputParsing.FormatDuration(TimeSpan.FromSeconds(Math.Clamp(s.TimerSeconds, 0, ushort.MaxValue)));

            _yLeftMin = s.YLeftMin; _yLeftMax = s.YLeftMax;
            _yRightMin = s.YRightMin; _yRightMax = s.YRightMax;
            _xMin = s.XMin; _xMax = s.XMax;
            if (_yLeftMin >= _yLeftMax) (_yLeftMin, _yLeftMax) = (0, 10);
            if (_yRightMin >= _yRightMax) (_yRightMin, _yRightMax) = (0, 30);
            if (_xMin >= _xMax) (_xMin, _xMax) = (0, 30);

            _autoScaleEnabled = s.AutoScaleEnabled;
            _measurementName = string.IsNullOrEmpty(s.MeasurementName) ? "Untitled" : s.MeasurementName;
            PlotModel.Title = _measurementName;

            _graphSettings = new GraphSettings
            {
                VoltageColor = GraphSettings.ParseColor(s.VoltageColor, OxyColors.Automatic),
                CurrentColor = GraphSettings.ParseColor(s.CurrentColor, OxyColors.Automatic),
                TemperatureColor = GraphSettings.ParseColor(s.TemperatureColor, OxyColors.Automatic),
                PowerColor = GraphSettings.ParseColor(s.PowerColor, OxyColors.Automatic),
                LegendBackground = GraphSettings.ParseColor(s.LegendBackground, OxyColors.Automatic),
                LegendBorder = GraphSettings.ParseColor(s.LegendBorder, OxyColors.Automatic)
            };

            // App.OnStartup has already applied the palette; keep the flag in sync
            _isDarkTheme = s.IsDarkTheme;
            if (ThemeManager.IsDark != _isDarkTheme)
                ThemeManager.ApplyTheme(_isDarkTheme);
        }

        private void SaveSettings(bool force = false)
        {
            if (_isLoading && !force) return;

            var s = AppSettings.Default;
            s.SelectedPort = SelectedPort;
            s.BaudRate = BaudRate;
            if (InputParsing.TryParseDouble(SetCurrentText, out double cur)) s.SetCurrent = cur;
            if (InputParsing.TryParseDouble(CutoffVoltageText, out double cut)) s.CutoffVoltage = cut;
            if (InputParsing.TryParseDuration(TimerText, out var timer)) s.TimerSeconds = (int)timer.TotalSeconds;
            s.YLeftMin = _yLeftMin; s.YLeftMax = _yLeftMax;
            s.YRightMin = _yRightMin; s.YRightMax = _yRightMax;
            s.XMin = _xMin; s.XMax = _xMax;
            s.AutoScaleEnabled = AutoScaleEnabled;
            s.MeasurementName = MeasurementName;
            s.VoltageColor = GraphSettings.FormatColor(GraphSettings.VoltageColor);
            s.CurrentColor = GraphSettings.FormatColor(GraphSettings.CurrentColor);
            s.TemperatureColor = GraphSettings.FormatColor(GraphSettings.TemperatureColor);
            s.PowerColor = GraphSettings.FormatColor(GraphSettings.PowerColor);
            s.LegendBackground = GraphSettings.FormatColor(GraphSettings.LegendBackground);
            s.LegendBorder = GraphSettings.FormatColor(GraphSettings.LegendBorder);
            s.IsDarkTheme = IsDarkTheme;
            s.Save();
        }

        // =====================================================================
        // INotifyPropertyChanged
        // =====================================================================
        public event PropertyChangedEventHandler? PropertyChanged;

        private void OnPropertyChanged([CallerMemberName] string? name = null) =>
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

        private bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value)) return false;
            field = value;
            OnPropertyChanged(name);
            return true;
        }

        private void SetBusy(bool busy)
        {
            _isBusy = busy;
            OnPropertyChanged(nameof(CanEditPort));
            CommandManager.InvalidateRequerySuggested();
        }

        private void RaiseStateChanged()
        {
            OnPropertyChanged(nameof(IsConnected));
            OnPropertyChanged(nameof(IsRunning));
            OnPropertyChanged(nameof(CanEditPort));
            OnPropertyChanged(nameof(ConnectButtonText));
            OnPropertyChanged(nameof(StartStopButtonText));
            OnPropertyChanged(nameof(LoadStateText));
            CommandManager.InvalidateRequerySuggested();
        }
    }
}
