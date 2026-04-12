System Telemetry is a compact Windows 11 dashboard for an 800x480 secondary display. It presents CPU, GPU, memory, network, storage, and time data in a high-contrast Win32 interface designed for always-on glanceability.

The app reads telemetry from Windows and vendor runtimes where available, including AMD ADLX for Radeon GPU metrics and Gigabyte SIV integration for supported board sensors.

For first-use setup, start the app, open the tray or window menu, use `Config To Display` to pick the target monitor, and then enable `Auto-start on user logon` if you want it to launch automatically after sign-in.

Build and development setup instructions live in [docs/build.md](docs/build.md).

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
