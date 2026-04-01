using System;
using System.Collections;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

internal static class GigabyteFanProbe
{
    private const string ToolDirectory = @"C:\Program Files (x86)\GIGABYTE\SIV";
    private const string EngineEnvironmentControlDll = "Gigabyte.Engine.EnvironmentControl.dll";
    private const string EnvironmentControlCommonDll = "Gigabyte.EnvironmentControl.Common.dll";

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool SetDllDirectory(string lpPathName);

    private static int Main()
    {
        try
        {
            if (!Directory.Exists(ToolDirectory))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte SIV directory was not found.");
                return 1;
            }

            var engineEnvironmentControlPath = Path.Combine(ToolDirectory, EngineEnvironmentControlDll);
            var environmentControlCommonPath = Path.Combine(ToolDirectory, EnvironmentControlCommonDll);
            if (!File.Exists(engineEnvironmentControlPath))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte.Engine.EnvironmentControl.dll was not found.");
                return 1;
            }
            if (!File.Exists(environmentControlCommonPath))
            {
                Write("success", "0");
                Write("diagnostics", "Gigabyte.EnvironmentControl.Common.dll was not found.");
                return 1;
            }

            Environment.CurrentDirectory = ToolDirectory;
            SetDllDirectory(ToolDirectory);
            AppDomain.CurrentDomain.AssemblyResolve += ResolveGigabyteAssembly;

            var engineAssembly = Assembly.LoadFrom(engineEnvironmentControlPath);
            var commonAssembly = Assembly.LoadFrom(environmentControlCommonPath);
            var hardwareMonitorType = engineAssembly.GetType(
                "Gigabyte.Engine.EnvironmentControl.HardwareMonitor.HardwareMonitorControlModule",
                true);
            var sourceType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitorSourceTypes",
                true);
            var sensorType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.SensorTypes",
                true);
            var sensorDataType = commonAssembly.GetType(
                "Gigabyte.EnvironmentControl.Common.HardwareMonitor.HardwareMonitoredData",
                true);
            var collectionType = commonAssembly.GetType(
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
                return 1;
            }

            var sourceSelection = Enum.Parse(sourceType, "HwRegister", false);
            initializeMethod.Invoke(monitor, new[] { sourceSelection });

            var sensorCollection = Activator.CreateInstance(collectionType);
            var arguments = new object[] { Enum.Parse(sensorType, "Fan", false), sensorCollection };
            getCurrentMethod.Invoke(monitor, arguments);
            sensorCollection = arguments[1];

            var titleProperty = sensorDataType.GetProperty("Title");
            var valueProperty = sensorDataType.GetProperty("Value");
            var unitProperty = sensorDataType.GetProperty("Unit");

            Write("success", "1");
            Write("diagnostics", "Gigabyte SIV hardware-monitor IPC completed.");
            Write("controller_type", "Gigabyte Engine HardwareMonitorControlModule");
            Write("chip_name", "Gigabyte SIV HwRegister");

            var fanIndex = 0;
            foreach (var sensor in (IEnumerable)sensorCollection)
            {
                var unit = Convert.ToString(unitProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
                if (!string.Equals(unit, "RPM", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                var title = Convert.ToString(titleProperty.GetValue(sensor, null), CultureInfo.InvariantCulture) ?? string.Empty;
                var value = valueProperty.GetValue(sensor, null);
                var rpm = Convert.ToSingle(value, CultureInfo.InvariantCulture);

                Write("fan_" + fanIndex.ToString(CultureInfo.InvariantCulture) + "_title", title);
                Write("fan_" + fanIndex.ToString(CultureInfo.InvariantCulture) + "_rpm", rpm.ToString(CultureInfo.InvariantCulture));
                fanIndex++;
            }

            Write("fan_count", fanIndex.ToString(CultureInfo.InvariantCulture));
            return fanIndex > 0 ? 0 : 2;
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

    private static Assembly ResolveGigabyteAssembly(object sender, ResolveEventArgs args)
    {
        var simpleName = new AssemblyName(args.Name).Name;
        if (string.IsNullOrEmpty(simpleName))
        {
            return null;
        }

        var candidatePath = Path.Combine(ToolDirectory, simpleName + ".dll");
        return File.Exists(candidatePath) ? Assembly.LoadFrom(candidatePath) : null;
    }

    private static void Write(string key, string value)
    {
        Console.WriteLine(key + "=" + value);
    }
}
