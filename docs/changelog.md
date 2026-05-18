## v0.2

- Added live dashboard animation for metric bars, gauges, drive activity, and throughput plots.
- DXGI swap-chain vsync-paced animation spends less than 1 ms per frame.
- Telemetry metrics update every 250 ms, and values interpolate smoothly between telemetry updates during animation.
- Added Intel GPU telemetry through Level Zero Sysman.
- Added ASUS Armoury Crate board telemetry.
- Supported machines with multiple GPUs via configuration/UI switch of the current GPU.

---
## v0.1.1

- Improved layout-edit drag feedback so guides, handles, and dragged content track continuous pointer movement more immediately.
- Reduced the shipped executable below 1 MB through compressed resources, leaner embedded atlases, smaller runtime metadata, and tighter cold-path code shape.
- Added branded .msi installer dialog and banner artwork generated during packaging.
- Fixed installer completion launch behavior that could trigger error 2753.
- Fixed throughput graph scaling so smoothed graph maxima stay stable when retained history scrolls.

---
## v0.1

- Initial release.
