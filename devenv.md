# Development Environment Contract

This file documents only the local `devenv.cmd` bootstrap contract.
Build prerequisites, build steps, and provider setup notes live in [docs/build.md](docs/build.md).

## Contract

- `devenv.cmd` prepares a Visual Studio 2022 x64 developer shell for this project.
- After `devenv.cmd` finishes, the current shell resolves `cl`, `link`, `rc`, `cmake`, and `MSBuild`.
- `devenv.cmd` exports `NETFXSDKDir` so the CMake build can resolve `mscoree.lib` for the mixed-mode Gigabyte provider build.
- Keep machine-specific toolchain paths and local installation details in `devenv.cmd`, not in `CMakeLists.txt` or the maintained docs.

