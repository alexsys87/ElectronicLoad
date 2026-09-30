using System;
using System.IO.Ports;

namespace Electronic_Load
{
    /// <summary><see cref="IPx100Transport"/> over System.IO.Ports.SerialPort (9600 8N1, no handshake).</summary>
    public sealed class SerialPortTransport : IPx100Transport, IDisposable
    {
        private readonly SerialPort _port;

        public SerialPortTransport(string portName, int baudRate = 9600, int readTimeoutMs = 1000)
        {
            _port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
            {
                Handshake = Handshake.None,
                ReadTimeout = readTimeoutMs,
                WriteTimeout = 1000
            };
        }

        public string PortName => _port.PortName;

        public bool IsOpen => _port.IsOpen;

        public void Open() => _port.Open();

        public void Write(byte[] buffer, int offset, int count) => _port.Write(buffer, offset, count);

        public int Read(byte[] buffer, int offset, int count) => _port.Read(buffer, offset, count);

        public void DiscardInBuffer() => _port.DiscardInBuffer();

        public void Dispose()
        {
            try
            {
                if (_port.IsOpen)
                    _port.Close();
            }
            catch
            {
                // The adapter may already be gone (USB unplugged); nothing to do.
            }
            _port.Dispose();
        }
    }
}
