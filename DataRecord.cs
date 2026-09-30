using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Electronic_Load
{
    /// <summary>One logged measurement.</summary>
    public class DataRecord
    {
        /// <summary>Wall-clock time of the sample; null for data imported from old CSV files.</summary>
        public DateTime? Time { get; set; }
        /// <summary>Seconds since the start of the session (X axis of the graph).</summary>
        public double Elapsed { get; set; }
        public double Voltage { get; set; }
        public double Current { get; set; }
        public double Power { get; set; }
        public double Capacity { get; set; }     // mAh
        public double Energy { get; set; }       // mWh
        public double Temperature { get; set; }  // degC

        public string ElapsedText => FormatElapsed(Elapsed);

        public static string FormatElapsed(double seconds)
        {
            var t = TimeSpan.FromSeconds(Math.Max(0, Math.Round(seconds)));
            return $"{(int)t.TotalHours:00}:{t.Minutes:00}:{t.Seconds:00}";
        }
    }

    /// <summary>CSV import/export. Always uses the invariant culture so files are portable.</summary>
    public static class DataCsv
    {
        // The first five columns keep the layout of the previous version (Time_s,Voltage_V,Current_A,Temperature_C,Power_W)
        public const string Header = "Time_s,Voltage_V,Current_A,Temperature_C,Power_W,Capacity_mAh,Energy_mWh,Timestamp";

        private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        public static void Write(TextWriter writer, IEnumerable<DataRecord> records)
        {
            writer.WriteLine(Header);
            var sb = new StringBuilder();
            foreach (var r in records)
            {
                sb.Clear();
                sb.Append(r.Elapsed.ToString("F3", Inv)).Append(',')
                  .Append(r.Voltage.ToString("F3", Inv)).Append(',')
                  .Append(r.Current.ToString("F3", Inv)).Append(',')
                  .Append(r.Temperature.ToString("F1", Inv)).Append(',')
                  .Append(r.Power.ToString("F3", Inv)).Append(',')
                  .Append(r.Capacity.ToString("F0", Inv)).Append(',')
                  .Append(r.Energy.ToString("F0", Inv)).Append(',')
                  .Append(r.Time.HasValue ? r.Time.Value.ToString("yyyy-MM-dd HH:mm:ss", Inv) : "");
                writer.WriteLine(sb.ToString());
            }
        }

        /// <summary>
        /// Reads files written by this and by the previous version (5 columns).
        /// Header and malformed lines are skipped; returns the number of skipped non-empty lines.
        /// </summary>
        public static List<DataRecord> Read(TextReader reader, out int skipped)
        {
            var list = new List<DataRecord>();
            skipped = 0;
            string? line;
            while ((line = reader.ReadLine()) != null)
            {
                if (string.IsNullOrWhiteSpace(line))
                    continue;

                var p = line.Split(',');
                if (p.Length < 5 ||
                    !TryNum(p[0], out double t) || !TryNum(p[1], out double v) || !TryNum(p[2], out double a) ||
                    !TryNum(p[3], out double temp) || !TryNum(p[4], out double w))
                {
                    if (!line.StartsWith("Time_s", StringComparison.OrdinalIgnoreCase))
                        skipped++;
                    continue;
                }

                var r = new DataRecord { Elapsed = t, Voltage = v, Current = a, Temperature = temp, Power = w };
                if (p.Length > 5 && TryNum(p[5], out double mah)) r.Capacity = mah;
                if (p.Length > 6 && TryNum(p[6], out double mwh)) r.Energy = mwh;
                if (p.Length > 7 && DateTime.TryParseExact(p[7].Trim(), "yyyy-MM-dd HH:mm:ss", Inv,
                        DateTimeStyles.None, out var ts))
                    r.Time = ts;
                list.Add(r);
            }
            return list;
        }

        private static bool TryNum(string s, out double value) =>
            double.TryParse(s.Trim(), NumberStyles.Float, Inv, out value);
    }

    /// <summary>Lenient number/time parsing for user input (accepts both '.' and ',' as decimal separator).</summary>
    public static class InputParsing
    {
        public static bool TryParseDouble(string? text, out double value)
        {
            value = 0;
            if (string.IsNullOrWhiteSpace(text))
                return false;
            string s = text.Trim().Replace(',', '.');
            return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out value)
                   && !double.IsNaN(value) && !double.IsInfinity(value);
        }

        /// <summary>Accepts "h:mm:ss", "mm:ss" or plain seconds.</summary>
        public static bool TryParseDuration(string? text, out TimeSpan value)
        {
            value = TimeSpan.Zero;
            if (string.IsNullOrWhiteSpace(text))
                return false;

            var parts = text.Trim().Split(':');
            if (parts.Length > 3)
                return false;

            long total = 0;
            foreach (var part in parts)
            {
                if (!int.TryParse(part.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out int n))
                    return false;
                total = total * 60 + n;
            }
            value = TimeSpan.FromSeconds(total);
            return true;
        }

        public static string FormatDuration(TimeSpan t) =>
            $"{(int)t.TotalHours:00}:{t.Minutes:00}:{t.Seconds:00}";

        public static string FormatNumber(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);
    }

    public static class StringExtensions
    {
        public static string ReplaceInvalidFileNameChars(this string filename) =>
            string.Join("_", filename.Split(Path.GetInvalidFileNameChars()));
    }
}
