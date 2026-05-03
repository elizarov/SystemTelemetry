# Release Workflow

This document owns CaseDash versioning and release publication.
See also: [docs/build.md](build.md) for local build setup and [docs/project.md](project.md) for repository policy.

## Version Source

- [VERSION](../VERSION) is the base product version.
- Version text uses `major.minor` or `major.minor.patch`.
- CMake reads `VERSION` and Git metadata during configure, then generates build metadata under `build\cmake\generated`.
- The generated metadata is compiled into the executable as C++ constants, a Windows `VERSIONINFO` resource, and the application manifest assembly version.
- The MSI package uses the same `VERSION` text as its filename version and normalizes `major.minor` to `major.minor.0` for the Windows Installer product version.

## Build Kinds

- An official release build comes from a clean commit whose exact Git tag is `v<VERSION>`.
- Official release builds report the plain version from `VERSION`.
- Other builds are development builds and report `VERSION-dev+g<commit>`.
- Locally modified builds append `.dirty`.
- Development builds set the Windows version resource prerelease flag.

## User-Visible Version

- Windows file properties show the numeric file version plus the full CaseDash version string.
- The application manifest uses the four-part Windows numeric version derived from `VERSION`.
- The dashboard menu exposes `About CaseDash` immediately before `Exit`.
- The About dialog shows the current themed app icon, full version, release kind, and commit when available.

## Official Release Steps

Use the repository release entrypoint:

```bat
release.cmd <version> [--force]
```

`release.cmd` asks for keyboard confirmation, updates [VERSION](../VERSION) when needed, commits that version change when it exists, runs format, lint, build, and tests, creates the matching annotated `v<VERSION>` tag, pushes the current branch, and pushes the tag.

Pass `--force` to replace an existing local or remote `v<VERSION>` tag after validation. The release workflow replaces an existing GitHub Release for that tag before publishing the rebuilt assets.

The `Release` GitHub Actions workflow checks that the tag matches `VERSION`, builds and tests CaseDash, packages the executable, builds the minimal x64 WiX MSI, writes SHA-256 checksums, creates the GitHub Release, builds the static website, and deploys `web\dist\` to GitHub Pages.

The release assets are the standalone executable, ZIP package, MSI installer, and matching `.sha256` files.

## Installer Behavior

- The MSI uses a branded no-license install-directory UI.
- The MSI installs only `CaseDash.exe` into `C:\Program Files\CaseDash`.
- The completion page offers a default-enabled option to run CaseDash immediately in front.
- Any other CaseDash MSI product version is replaceable so dev and release packages do not register side by side.
- MSI upgrades keep runtime-owned files beside `CaseDash.exe`, including the executable-side `config.ini` and `casedash_blank.png`, while replacing the executable.
- Runtime auto-start and service registration remain owned by the app menu.
- MSI uninstall closes a running `CaseDash.exe` before file removal, then removes the install directory tree, the `CaseDash` machine-wide Run value, and the `CashDashService` service when present.

The release workflow can also be started manually with a tag input, but the tag must still match `VERSION`. The separate `Pages` workflow is manual-only and rebuilds and deploys the website from a selected branch, tag, or commit without creating a release.
