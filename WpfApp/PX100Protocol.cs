using System;
using System.Diagnostics;

namespace Electronic_Load
{
    /// <summary>
    /// Byte transport used by <see cref="PX100Protocol"/>. Abstracted so the protocol
    /// can be tested against an emulator without a physical serial port.
    /// </summary>
    public interface IPx100Transport
    {
        /// <summary>Writes the whole buffer range.</summary>
        void Write(byte[] buffer, int offset, int count);

        /// <summary>
        /// Reads at least one byte and returns the number of bytes read.
        /// Must throw <see cref="TimeoutException"/> when nothing arrives within the read timeout.
        /// </summary>
        int Read(byte[] buffer, int offset, int count);

        /// <summary>Drops any stale bytes waiting in the receive buffer.</summary>
        void DiscardInBuffer();
    }

    /// <summary>Protocol-level failure: no answer, malformed frame or failed verification.</summary>
    public sealed class Px100Exception : Exception
    {
        public Px100Exception(string message, Exception? inner = null) : base(message, inner) { }
    }

    /// <summary>One set of live readings.</summary>
    public sealed class Px100Snapshot
    {
        /// <summary>Host time at which the output state was read.</summary>
        public DateTime Timestamp { get; init; }
        public bool IsOn { get; init; }
        public double Voltage { get; init; }        // V
        public double Current { get; init; }        // A
        public double CapacityMAh { get; init; }    // mAh
        public double EnergyMWh { get; init; }      // mWh
        public double Temperature { get; init; }    // degC (integer resolution)
        public TimeSpan Elapsed { get; init; }      // device test time
    }

    /// <summary>Setpoints stored in the device.</summary>
    public sealed class Px100Setpoints
    {
        public double Current { get; init; }        // A
        public double CutoffVoltage { get; init; }  // V
        public TimeSpan Timer { get; init; }        // 0 = timer disabled
    }

    /// <summary>
    /// PX-100 (board rev. 2.70 / 2.8) binary protocol, 9600 8N1, no flow control.
    /// Reference: github.com/misdoro/Electronic_load_px100 (protocol_PX-100_2_70.md, instruments/px100.py).
    ///
    /// Request  : B1 B2 CMD D1 D2 B6
    /// Control  : CMD 0x01..0x05, answer is a single byte 0x6F
    ///            0x01 on/off  : D1=0x01 on, 0x00 off, D2=0x00
    ///            0x02 current : D1 = integer part, D2 = fraction * 100 (A)
    ///            0x03 cutoff  : D1 = integer part, D2 = fraction * 100 (V)
    ///            0x04 timer   : D1:D2 = 16-bit unsigned seconds, big-endian
    ///            0x05 reset   : 00 00, clears mAh/mWh/time counters
    /// Query    : CMD 0x10..0x19 with D1=D2=0, answer CA CB d1 d2 d3 CE CF
    ///            0x10 on/off (24-bit, 1 = on), 0x11 mV, 0x12 mA, 0x13 time h:m:s,
    ///            0x14 mAh, 0x15 mWh, 0x16 degC, 0x17 set current A*100,
    ///            0x18 cutoff V*100, 0x19 timer setting h:m:s
    ///
    /// All public members are thread-safe; one transaction owns the port at a time.
    /// Calls are blocking, run them off the UI thread.
    /// </summary>
    public sealed class PX100Protocol
    {
        // Frame bytes
        private const byte ReqHead1 = 0xB1;
        private const byte ReqHead2 = 0xB2;
        private const byte ReqTail = 0xB6;
        private const byte RespHead1 = 0xCA;
        private const byte RespHead2 = 0xCB;
        private const byte RespTail1 = 0xCE;
        private const byte RespTail2 = 0xCF;
        private const byte Ack = 0x6F;

        // Control commands
        public const byte CmdOutput = 0x01;
        public const byte CmdSetCurrent = 0x02;
        public const byte CmdSetCutoff = 0x03;
        public const byte CmdSetTimer = 0x04;
        public const byte CmdResetCounters = 0x05;

        // Query commands
        public const byte QryIsOn = 0x10;
        public const byte QryVoltage = 0x11;
        public const byte QryCurrent = 0x12;
        public const byte QryTime = 0x13;
        public const byte QryCapacityMAh = 0x14;
        public const byte QryEnergyMWh = 0x15;
        public const byte QryTemperature = 0x16;
        public const byte QrySetCurrent = 0x17;
        public const byte QrySetCutoff = 0x18;
        public const byte QryTimer = 0x19;

        /// <summary>Largest value representable as D1.D2 (D1 is one byte).</summary>
        public const double MaxFixedPointValue = 255.99;

        /// <summary>Largest timer value (16-bit seconds).</summary>
        public static readonly TimeSpan MaxTimer = TimeSpan.FromSeconds(ushort.MaxValue);

        private const int MaxJunkBytes = 64;

        private readonly IPx100Transport _transport;
        private readonly object _sync = new();

        /// <summary>Attempts per request/response exchange (timeouts, broken frames).</summary>
        public int TransactionAttempts { get; set; } = 3;

        /// <summary>Attempts per verified setting (write, read back, compare).</summary>
        public int VerifyAttempts { get; set; } = 3;

        /// <summary>Pause before reading a setting back; the firmware needs time to apply it.</summary>
        public int VerifyDelayMs { get; set; } = 150;

        public PX100Protocol(IPx100Transport transport)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        }

        // ================= Encoding helpers =================

        /// <summary>Encodes a value as D1 = integer part, D2 = hundredths (0..99).</summary>
        public static (byte D1, byte D2) EncodeFixedPoint(double value, string what)
        {
            if (double.IsNaN(value) || double.IsInfinity(value))
                throw new ArgumentOutOfRangeException(nameof(value), $"{what}: invalid number");

            // Round to hundredths first so 1.999 becomes 2.00 and never D2 = 100
            long centi = (long)Math.Round(value * 100.0, MidpointRounding.AwayFromZero);
            if (centi < 0 || centi > (long)Math.Round(MaxFixedPointValue * 100))
                throw new ArgumentOutOfRangeException(nameof(value),
                    $"{what} must be within 0 .. {MaxFixedPointValue:0.00}");

            return ((byte)(centi / 100), (byte)(centi % 100));
        }

        /// <summary>Encodes a timer value as a big-endian 16-bit number of seconds.</summary>
        public static (byte D1, byte D2) EncodeTimer(TimeSpan timer)
        {
            double seconds = Math.Round(timer.TotalSeconds);
            if (seconds < 0 || seconds > ushort.MaxValue)
                throw new ArgumentOutOfRangeException(nameof(timer),
                    $"Timer must be within 00:00:00 .. {MaxTimer:hh\\:mm\\:ss}");

            ushort s = (ushort)seconds;
            return ((byte)(s >> 8), (byte)(s & 0xFF));
        }

        private static int Int24(byte[] d) => (d[0] << 16) | (d[1] << 8) | d[2];

        private static TimeSpan Hms(byte[] d) => new TimeSpan(d[0], d[1], d[2]);

        // ================= Link layer =================

        private byte ReadByte()
        {
            var one = new byte[1];
            int n = _transport.Read(one, 0, 1);
            if (n <= 0)
                throw new TimeoutException("Serial read returned no data");
            return one[0];
        }

        private void WaitAck(byte cmd)
        {
            // Anything before 0x6F is late junk from an earlier exchange
            for (int junk = 0; junk <= MaxJunkBytes; junk++)
            {
                if (ReadByte() == Ack)
                    return;
            }
            throw new Px100Exception($"Command 0x{cmd:X2}: no ACK (0x6F) in the received data");
        }

        private byte[] ReadQueryFrame(byte cmd)
        {
            var data = new byte[3];
            int state = 0;
            int junk = 0;

            while (true)
            {
                byte b = ReadByte();
                switch (state)
                {
                    case 0:
                        if (b == RespHead1) state = 1;
                        else junk++;
                        break;
                    case 1:
                        if (b == RespHead2) state = 2;
                        else if (b != RespHead1) { state = 0; junk++; }
                        break;
                    case 2:
                    case 3:
                    case 4:
                        data[state - 2] = b;
                        state++;
                        break;
                    case 5:
                        if (b != RespTail1)
                            throw new Px100Exception($"Query 0x{cmd:X2}: bad frame tail 0x{b:X2}");
                        state = 6;
                        break;
                    case 6:
                        if (b != RespTail2)
                            throw new Px100Exception($"Query 0x{cmd:X2}: bad frame tail 0x{b:X2}");
                        return data;
                }

                if (junk > MaxJunkBytes)
                    throw new Px100Exception($"Query 0x{cmd:X2}: no response header in the received data");
            }
        }

        /// <summary>
        /// Sends one request and waits for its answer, retrying on timeouts and broken frames.
        /// Returns the 3 data bytes for queries, an empty array for control commands.
        /// Port-level exceptions (IOException, InvalidOperationException) are not retried.
        /// </summary>
        private byte[] Transact(byte cmd, byte d1 = 0, byte d2 = 0)
        {
            var frame = new byte[] { ReqHead1, ReqHead2, cmd, d1, d2, ReqTail };
            Exception? last = null;

            lock (_sync)
            {
                for (int attempt = 0; attempt < Math.Max(1, TransactionAttempts); attempt++)
                {
                    try
                    {
                        _transport.DiscardInBuffer();
                        _transport.Write(frame, 0, frame.Length);

                        if (cmd < 0x10)
                        {
                            WaitAck(cmd);
                            return Array.Empty<byte>();
                        }
                        return ReadQueryFrame(cmd);
                    }
                    catch (TimeoutException ex) { last = ex; }
                    catch (Px100Exception ex) { last = ex; }
                }
            }

            throw new Px100Exception(
                $"No valid answer to 0x{cmd:X2} after {TransactionAttempts} attempt(s): {last?.Message}", last);
        }

        /// <summary>Writes a setting, reads it back and repeats until it matches.</summary>
        private void WriteVerified(byte cmd, byte d1, byte d2, Func<bool> verify, string what)
        {
            lock (_sync)
            {
                for (int attempt = 0; attempt < Math.Max(1, VerifyAttempts); attempt++)
                {
                    Transact(cmd, d1, d2);
                    if (VerifyDelayMs > 0)
                        System.Threading.Thread.Sleep(VerifyDelayMs);
                    if (verify())
                        return;
                    Debug.WriteLine($"PX100: {what} not applied, retry {attempt + 1}");
                }
            }
            throw new Px100Exception($"{what}: device did not accept the new value");
        }

        // ================= Control commands =================

        /// <summary>Switches the load on or off (verified with query 0x10).</summary>
        public void SetOutput(bool on)
        {
            WriteVerified(CmdOutput, (byte)(on ? 0x01 : 0x00), 0x00,
                () => IsOn() == on, on ? "Load ON" : "Load OFF");
        }

        /// <summary>Sets the load current in amperes, 0.01 A resolution (verified with 0x17).</summary>
        public void SetCurrent(double amps)
        {
            var (d1, d2) = EncodeFixedPoint(amps, "Current");
            double expected = d1 + d2 / 100.0;
            WriteVerified(CmdSetCurrent, d1, d2,
                () => Math.Abs(GetSetCurrent() - expected) < 0.005, "Set current");
        }

        /// <summary>Sets the cutoff voltage in volts, 0.01 V resolution (verified with 0x18).</summary>
        public void SetCutoffVoltage(double volts)
        {
            var (d1, d2) = EncodeFixedPoint(volts, "Cutoff voltage");
            double expected = d1 + d2 / 100.0;
            WriteVerified(CmdSetCutoff, d1, d2,
                () => Math.Abs(GetCutoffVoltage() - expected) < 0.005, "Set cutoff voltage");
        }

        /// <summary>Sets the discharge timer, 0 disables it (verified with 0x19).</summary>
        public void SetTimer(TimeSpan timer)
        {
            var (d1, d2) = EncodeTimer(timer);
            var expected = TimeSpan.FromSeconds((d1 << 8) | d2);
            WriteVerified(CmdSetTimer, d1, d2, () => GetTimer() == expected, "Set timer");
        }

        /// <summary>Clears the mAh / mWh / time counters.</summary>
        public void ResetCounters()
        {
            lock (_sync)
            {
                double before = GetCapacityMAh();
                // With the load running the counter restarts immediately,
                // so "dropped below the previous value" also counts as success.
                WriteVerified(CmdResetCounters, 0x00, 0x00, () =>
                {
                    double after = GetCapacityMAh();
                    return after == 0 || after < before;
                }, "Reset counters");
            }
        }

        // ================= Queries =================

        public bool IsOn() => Int24(Transact(QryIsOn)) != 0;

        public double GetVoltage() => Int24(Transact(QryVoltage)) / 1000.0;         // mV -> V

        public double GetCurrent() => Int24(Transact(QryCurrent)) / 1000.0;         // mA -> A

        public TimeSpan GetElapsedTime() => Hms(Transact(QryTime));

        public double GetCapacityMAh() => Int24(Transact(QryCapacityMAh));          // mAh

        public double GetEnergyMWh() => Int24(Transact(QryEnergyMWh));              // mWh

        public double GetTemperature() => Int24(Transact(QryTemperature));          // whole degC

        public double GetSetCurrent() => Int24(Transact(QrySetCurrent)) / 100.0;    // A*100 -> A

        public double GetCutoffVoltage() => Int24(Transact(QrySetCutoff)) / 100.0;  // V*100 -> V

        public TimeSpan GetTimer() => Hms(Transact(QryTimer));

        /// <summary>Checks that a PX-100 answers on this port.</summary>
        public bool Probe()
        {
            try
            {
                GetVoltage();
                return true;
            }
            catch (Px100Exception)
            {
                return false;
            }
        }

        /// <summary>Reads all live values in one go.</summary>
        public Px100Snapshot ReadSnapshot()
        {
            lock (_sync)
            {
                var ts = DateTime.Now;
                bool on = IsOn();
                return new Px100Snapshot
                {
                    Timestamp = ts,
                    IsOn = on,
                    Voltage = GetVoltage(),
                    Current = GetCurrent(),
                    CapacityMAh = GetCapacityMAh(),
                    EnergyMWh = GetEnergyMWh(),
                    Temperature = GetTemperature(),
                    Elapsed = GetElapsedTime()
                };
            }
        }

        /// <summary>Reads the setpoints stored in the device.</summary>
        public Px100Setpoints ReadSetpoints()
        {
            lock (_sync)
            {
                return new Px100Setpoints
                {
                    Current = GetSetCurrent(),
                    CutoffVoltage = GetCutoffVoltage(),
                    Timer = GetTimer()
                };
            }
        }
    }
}
