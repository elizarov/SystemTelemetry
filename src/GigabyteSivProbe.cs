using System;
using System.Collections;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32;

internal static class GigabyteSivProbe
{
    private const string EngineEnvironmentControlDll = "Gigabyte.Engine.EnvironmentControl.dll";
    private const string EnvironmentControlCommonDll = "Gigabyte.EnvironmentControl.Common.dll";

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool SetDllDirectory(string lpPathName);

    private static int Main(string[] args)
    {
        try
        {
            var daemonMode = args.Length > 0 && string.Equals(args[0], "/daemon", StringComparison.OrdinalIgnoreCase);
            Type hardwareMonitorType;
            Type sourceType;
            Type sensorType;
            Type sensorDataType;
            Type collectionType;
            var monitor = CreateMonitor(out hardwareMonitorType, out sourceType, out sensorType, out sensorDataType, out collectionType);
            if (monitor == null)
            {
                return 1;
            }

            if (daemonMode)
            {
                while (true)
                {
                    WriteFrame(CaptureSnapshot(monitor, hardwareMonitorType, sourceType, sensorType, sensorDataType, collectionType), true);
                    Thread.Sleep(1000);
                }
            }

            var snapshot = CaptureSnapshot(monitor, hardwareMonitorType, sourceType, sensorType, sensorDataType, collectionType);
            WriteFrame(snapshot, false);
            return snapshot.success && (snapshot.fanCount > 0 || snapshot.temperatureCount > 0) ? 0 : 2;
        }
        catch (Exception ex)
        {
            Write("success", "0");
            Write("diagnostics", ex.ToString());
            return 1;
        }
        finally
        {
            SetDllDirectory(null);
        }
    }

    private static object CreateMonitor(
        out Type hardwareMonitorType,
        out Type sourceType,
        out Type sensorType,
        out Type sensorDataType,
        out Type collectionType)
    {
        hardwareMonitorType = null;
        sourceType = null;
        sensorType = null;
        sensorDataType = null;
        collectionType = null;

        try
        {
            var toolDirectory = DiscoverToolDirectory();
            if (string.IsNullOrEmpty(toolDirectory) || !Directory.Exists(toolDirectory))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte SIV directory was not found in the registry.");
                return null;
            }

            var engineEnvironmentControlPath = Path.Combine(toolDirectory, EngineEnvironmentControlDll);
            var environmentControlCommonPath = Path.Combine(toolDirectory, EnvironmentControlCommonDll);
            if (!File.Exists(engineEnvironmentControlPath))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte.Engine.EnvironmentControl.dll was not found.");
                return null;
            }
            if (!File.Exists(environmentControlCommonPath))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte.EnvironmentControl.Common.dll was not found.");
                return null;
            }

            Environment.CurrentDirectory = toolDirectory;
            SetDllDirectory(toolDirectory);
            AppDomain.CurrentDomain.AssemblyResolve += ResolveGigabyteAssembly;

            var engineAssembly = Assembly.LoadFrom(engineEnvironmentControlPath);
            var commonAssembly = Assembly.LoadFrom(environmentControlCommonPath);
            hardwareMonitorType = engineAssembly.GetType(
                "Gigabyte.Engine.EnvironmentControl.HardwareMonitor.HardwareMonitorControlModule",
                true);
            sourceType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitorSourceTypes",
                true);
            sensorType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.SensorTypes",
                true);
            sensorDataType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredData",
                true);
            collectionType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredDataCollection",
                true);

            var monitor = Activator.CreateInstance(hardwareMonitorType);
            var initializeMethod = hardwareMonitorType.GetMethod(
                "Initialize",
                new[] { sourceType });
            var getCurrentMethod = hardwareMonitorType.GetMethod(
                "GetCurrentMonitoredData",
                new[] { sensorType, collectionType.MakeByRefType() });
            if (initializeMethod == null || getCurrentMethod == null)
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte hardware-monitor IPC methods were not found.");
                return null;
            }

            var sourceSelection = Enum.Parse(sourceType, "HwRegister", false);
            initializeMethod.Invoke(monitor, new[] { sourceSelection });
            return monitor;
        }
        catch (Exception ex)
        {
            Write("success", "0");
            Write("diagnostics", ex.ToString());
            return null;
        }
    }

    private static Snapshot CaptureSnapshot(
        object monitor,
        Type hardwareMonitorType,
        Type sourceType,
        Type sensorType,
        Type sensorDataType,
        Type collectionType)
    {
        var snapshot = new Snapshot
        {
            success = true,
            diagnostics = "Gigabyte SIV hardware-monitor IPC completed.",
            controllerType = "Gigabyte Engine HardwareMonitorControlModule",
            chipName = "Gigabyte SIV HwRegister"
        };

        var getCurrentMethod = hardwareMonitorType.GetMethod(
            "GetCurrentMonitoredData",
            new[] { sensorType, collectionType.MakeByRefType() });
        var titleProperty = sensorDataType.GetProperty("Title");
        var valueProperty = sensorDataType.GetProperty("Value");
        var unitProperty = sensorDataType.GetProperty("Unit");

        foreach (var sensor in ReadSensors(getCurrentMethod, monitor, sensorType, collectionType, "Fan"))
        {
            var unit = Convert.ToString(unitProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
            if (!string.Equals(unit, "RPM", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var title = Convert.ToString(titleProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
            var value = valueProperty.GetValue(sensor, null);
            var rpm = Convert.ToSingle(value, CultureInfo.InvariantCulture);
            snapshot.fans.Add(new FanSnapshot
            {
                title = title,
                rpm = rpm
            });
        }

        foreach (var sensor in ReadSensors(getCurrentMethod, monitor, sensorType, collectionType, "Temperature"))
        {
            var unit = Convert.ToString(unitProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
            if (!string.Equals(unit, "\u2103", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(unit, "\u00B0C", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var title = Convert.ToString(titleProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
            var value = valueProperty.GetValue(sensor, null);
            var temperatureC = Convert.ToSingle(value, CultureInfo.InvariantCulture);
            snapshot.temperatures.Add(new TemperatureSnapshot
            {
                title = title,
                celsius = temperatureC
            });
        }

        snapshot.fanCount = snapshot.fans.Count;
        snapshot.temperatureCount = snapshot.temperatures.Count;
        return snapshot;
    }

    private static IEnumerable ReadSensors(
        MethodInfo getCurrentMethod,
        object monitor,
        Type sensorType,
        Type collectionType,
        string sensorName)
    {
        var sensorCollection = Activator.CreateInstance(collectionType);
        var arguments = new object[] { Enum.Parse(sensorType, sensorName, false), sensorCollection };
        getCurrentMethod.Invoke(monitor, arguments);
        return (IEnumerable)arguments[1];
    }

    private static Assembly ResolveGigabyteAssembly(object sender, ResolveEventArgs args)
    {
        var simpleName = new AssemblyName(args.Name).Name;
        if (string.IsNullOrEmpty(simpleName))
        {
            return null;
        }

        var toolDirectory = DiscoverToolDirectory();
        if (string.IsNullOrEmpty(toolDirectory))
        {
            return null;
        }

        var candidatePath = Path.Combine(toolDirectory, simpleName + ".dll");
        return File.Exists(candidatePath) ? Assembly.LoadFrom(candidatePath) : null;
    }

    private static string DiscoverToolDirectory()
    {
        string installLocation;
        if (TryReadUninstallInstallLocation(out installLocation))
        {
            return installLocation;
        }
        return null;
    }

    private static bool TryReadUninstallInstallLocation(out string installLocation)
    {
        installLocation = null;
        var subKey = @"SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall";
        using (var uninstallKey = Registry.LocalMachine.OpenSubKey(subKey))
        {
            if (uninstallKey == null)
            {
                return false;
            }

            foreach (var childName in uninstallKey.GetSubKeyNames())
            {
                using (var childKey = uninstallKey.OpenSubKey(childName))
                {
                    if (childKey == null)
                    {
                        continue;
                    }

                    var displayName = childKey.GetValue("DisplayName") as string;
                    if (!string.Equals(displayName, "SIV", StringComparison.OrdinalIgnoreCase) &&
                        !string.Equals(displayName, "System Information Viewer", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    var candidate = childKey.GetValue("InstallLocation") as string;
                    if (string.IsNullOrWhiteSpace(candidate))
                    {
                        continue;
                    }

                    candidate = candidate.Trim();
                    if (Directory.Exists(candidate))
                    {
                        installLocation = candidate;
                        return true;
                    }
                }
            }
        }

        return false;
    }

    private static void Write(string key, string value)
    {
        Console.WriteLine(key + "=" + value);
    }

    private static void WriteFrame(Snapshot snapshot, bool framed)
    {
        if (framed)
        {
            Console.WriteLine("frame_begin");
        }

        Write("success", snapshot.success ? "1" : "0");
        Write("diagnostics", snapshot.diagnostics);
        Write("controller_type", snapshot.controllerType);
        Write("chip_name", snapshot.chipName);
        for (var i = 0; i < snapshot.fans.Count; i++)
        {
            Write("fan_" + i.ToString(CultureInfo.InvariantCulture) + "_title", snapshot.fans[i].title);
            Write("fan_" + i.ToString(CultureInfo.InvariantCulture) + "_rpm", snapshot.fans[i].rpm.ToString(CultureInfo.InvariantCulture));
        }
        Write("fan_count", snapshot.fanCount.ToString(CultureInfo.InvariantCulture));
        for (var i = 0; i < snapshot.temperatures.Count; i++)
        {
            Write("temp_" + i.ToString(CultureInfo.InvariantCulture) + "_title", snapshot.temperatures[i].title);
            Write("temp_" + i.ToString(CultureInfo.InvariantCulture) + "_c", snapshot.temperatures[i].celsius.ToString(CultureInfo.InvariantCulture));
        }
        Write("temp_count", snapshot.temperatureCount.ToString(CultureInfo.InvariantCulture));

        if (framed)
        {
            Console.WriteLine("frame_end");
            Console.Out.Flush();
        }
    }

    private sealed class Snapshot
    {
        public bool success;
        public string diagnostics;
        public string controllerType;
        public string chipName;
        public int fanCount;
        public int temperatureCount;
        public readonly System.Collections.Generic.List<FanSnapshot> fans = new System.Collections.Generic.List<FanSnapshot>();
        public readonly System.Collections.Generic.List<TemperatureSnapshot> temperatures = new System.Collections.Generic.List<TemperatureSnapshot>();
    }

    private sealed class FanSnapshot
    {
        public string title;
        public float rpm;
    }

    private sealed class TemperatureSnapshot
    {
        public string title;
        public float celsius;
    }
}
