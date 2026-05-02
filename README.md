# CaseDash

CaseDash is a compact Windows dashboard for dedicated PC telemetry screens: small USB/HDMI panels, case-mounted displays, or a secondary screen beside the main monitor. It presents CPU, GPU, memory, network, storage, board sensors, and time in a glanceable native interface built for always-on visibility.

It is not a generic hardware-monitoring suite. CaseDash is focused on making a polished sensor panel easy to place, theme, edit, and run every day on a small display. Place it and forget it &mdash; let it work. It is not something you constantly interact with, so there are no keyboard shortcuts and no extra interactivity beyond what is needed to configure it to your liking.

It works on my machines, with the hardware I have. Contributions are welcome for everything else.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/image/casedash-screenshot-dark.png">
  <source media="(prefers-color-scheme: light)" srcset="docs/image/casedash-screenshot-light.png">
  <img alt="CaseDash dashboard with built-in fake telemetry" src="docs/image/casedash-screenshot-light.png" width="800">
</picture>

## Who and why?

It's me, [Roman Elizarov](https://github.com/elizarov), of ICPC and Kotlin fame. Why a native C++ app? Because I can. It is an experiment for myself in what is possible to build: a tiny, fast, native dashboard with a real layout system, rich themes, and no painful pixel pushing. I'd be glad if it is useful for you, too.

## Highlights

- Constraint-based layouts that avoid tedious pixel alignment.
- Live layout editor with visual guides, draggable structure, and focused field editors.
- Theme system that derives a full dashboard palette from a small set of key colors.
- Small, fast native executable optimized for dashboard use: just over a 1 MB `.exe`, with frame drawing measured in milliseconds.
- Layouts for small panels from 5-inch 800x480 screens up to wide 9-inch 1920x480 screens.
- Built-in secondary-display setup through `Config To Display`, including wallpaper setup so the dash screen looks intentional even while the app starts.
- Keeps its configured monitor, position, and scale without extra placement software.
- Machine-wide auto-start setup works for all users on the PC out of the box.
- Tray and dashboard menus for layout, theme, scale, device selection, display setup, and auto-start.

## Supported Hardware

- Windows 11 PCs with a small secondary display, commonly 800x480 or similar.
- CPU, memory, network, storage, clock, and display data from Windows.

Graphics:

- AMD Radeon GPU telemetry through AMD ADLX.
- NVIDIA GPU telemetry through NVML, plus presented-FPS capture through Windows ETW.

Motherboards:

- Gigabyte board temperature and fan telemetry through Gigabyte SIV.
- MSI board temperature and fan telemetry through MSI Center SDK.

## First Use

1. Download and run `CaseDash.exe`.
2. Right-click the dashboard or tray icon.
3. Pick a layout and theme.
4. Use `Config To Display` to choose the small screen. It computes the correct full-screen scale and configures its startup wallpaper.
5. Enable `Auto-start on user logon` when the panel is ready for daily use.

Configuration is saved beside the executable as `config.ini`; the embedded default config remains the baseline when no local config exists.

## Contributions

Contributions are welcome in code, issues, sketches, and plain ideas.

Good areas to explore:

- New themes and new visual ideas. Fancier widgets? Animations? Everything is welcome as long as the core purpose is maintained.
- New layouts for different screen sizes and mounting styles.
- New GPU telemetry modules.
- New motherboard sensor modules.
- All great ideas are welcome. Open an issue and write what you want to achieve.

Linux users: are you interested? What hardware do you have? A Linux port would be a cool project to undertake; write up your use cases.

## Build And Docs

Build and development setup lives in [docs/build.md](docs/build.md). Release steps live in [docs/release.md](docs/release.md). Product behavior is specified in [docs/specifications.md](docs/specifications.md), and the layout/config language in [docs/layout.md](docs/layout.md).

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
