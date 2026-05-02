# Website Specification Proposal

This document proposes the public CaseDash website behavior, content, build flow, and generated-asset contracts.

## Purpose

The website is the end-user introduction to CaseDash. It presents what the app is, how to download it, what hardware it supports, how first use works, how layout editing looks, and where to contribute.

The site takes its tone and structure from the README: compact, practical, and visually led by the actual dashboard. It stays consistent across sections and avoids repeating product claims, configuration syntax, or diagnostics details that are already owned by maintained docs.

## Audience

The primary visitor owns or is considering a small telemetry display for a Windows PC. The page assumes the visitor wants to quickly answer:

- What CaseDash looks like.
- Whether their hardware is likely to work.
- How to install and configure it.
- How layouts and themes can be adjusted.
- Where to download, report issues, or contribute.

The secondary visitor is a potential contributor who wants a concise project overview before opening GitHub.

## Site Shape

The website is a single static page with continuous top-to-bottom scrolling and a persistent top navigation bar. Navigation anchors scroll to page sections instead of loading separate routes.

Required sections:

- `Intro`
- `Download`
- `Features`
- `Hardware`
- `Layout`
- `Contributions`

The page may include a compact footer for license, repository, release, and documentation links. The footer is not a primary section in top navigation.

## Intro Section

The intro section is the first viewport signal and identifies the product as `CaseDash`.

The section includes:

- A short product description aligned with the README overview.
- Primary links for download and the GitHub repository.
- A dashboard screenshot for the active website theme.
- A short first-use path: download, run, right-click, choose layout and theme, configure display, enable startup when ready.

The intro copy stays practical and avoids marketing-only language. The screenshot is the dominant visual object.

## Download Section

The download section follows the intro section. It includes:

- A primary link to `https://github.com/casedash/casedash/releases/latest`.
- A secondary link to all releases.
- A short note that CaseDash is a Windows executable and stores configuration beside `CaseDash.exe` as `config.ini`.
- A link to the repository for users who want to inspect source or build locally.

The section keeps release details brief and links to GitHub as the source of truth for versioned artifacts.

## Features Section

The features section summarizes the product capabilities that matter to end users:

- Compact always-on Windows telemetry dashboard.
- Constraint-based layouts and live layout editing.
- Theme system derived from a small set of base colors.
- Small native executable and fast frame rendering.
- Layouts for common small-panel formats.
- Built-in display setup, placement restore, startup wallpaper, and Start with Windows support.

Each feature item uses short copy and, where useful, a small cropped screenshot or visual treatment generated from actual app output. The section does not restate the full configuration language.

## Hardware Section

The hardware section lists currently supported telemetry families:

- AMD Radeon GPU telemetry through ADLX.
- NVIDIA GPU telemetry through NVML, plus presented-FPS capture through ETW.
- Gigabyte board telemetry through SIV.
- MSI board telemetry through MSI Center SDK.
- Windows-native CPU, memory, network, storage, clock, and display behavior.

The section also sets expectations:

- Unsupported vendor-specific sensors show unavailable values instead of making the app unusable.
- Contributions are welcome for additional GPU, motherboard, and sensor providers.
- Linux support is not currently a shipped target.

## Layout Section

The layout section introduces layout selection and layout editing from an end-user perspective.

The section includes:

- A short explanation that CaseDash uses named static layouts sized for small displays.
- The generated layout guide sheet for the active website theme.
- A concise explanation of what the guide sheet shows: editable cards, widget guides, spacing controls, and hover-style callouts.
- A link to `docs/layout.md` for the maintained layout language reference.

The page does not duplicate layout syntax examples unless the project later designates this website copy as the maintained source for a specific example.

## Contributions Section

The contributions section keeps detail light and sends visitors to GitHub.

It includes links for:

- Repository.
- Issues.
- Releases.
- Project documentation.

Suggested contribution areas:

- New themes and visual ideas.
- New layouts for different screen sizes and mounting styles.
- Additional telemetry providers.
- Localization and copy improvements.

## Design System

The website uses a minimal visual style that echoes CaseDash itself and stays consistent from section to section:

- High-contrast dashboard-like surfaces.
- Compact typography.
- Restrained separators and rounded card chrome.
- Glanceable metric-style blocks where useful.
- Copy and spacing inspired by the README's directness.
- No large marketing-card composition that competes with the product screenshot.

The website derives its visual styling from the same four theme tokens used by the app:

- `background`
- `foreground`
- `accent`
- `guide`

The website build extracts those tokens from `resources/config.ini`. Runtime CSS derives secondary colors from the four tokens rather than storing complete per-theme CSS.

The top-right navigation area contains a theme switcher. The switcher lists all app themes found in `resources/config.ini`, shows the active theme name, and applies the selected theme without reloading the page.

The default website theme follows the app's default embedded config theme. If the browser has no stored user choice, the site may choose a light or dark theme based on `prefers-color-scheme` only when that choice maps to a real app theme.

## Generated Assets

The website source does not manually mirror screenshots, guide sheets, or theme metadata for each theme.

The website build generates, for every configured app theme:

- A dashboard screenshot using the default layout.
- A layout guide sheet using the default layout.
- A metadata entry with theme id, description, and the four base tokens.

Generated dashboard screenshots use:

- Built-in default config behavior.
- Fake telemetry.
- Default layout.
- Render scale `2`.
- The selected theme.

Generated layout guide sheets use the same config, fake/default data, scale, and selected theme. The guide sheet generation uses the diagnostics layout-guide-sheet output instead of a separate web-only renderer.

Generated screenshots and guide sheets are not committed. Generated files are written under the web build output so repository roots and documentation image folders stay clean.

## Source Layout

Website source lives under `web/`.

Proposed layout:

```text
web/
  index.html
  src/
    main.js
    styles.css
  scripts/
    build-web.ps1
  dist/
```

`web/dist/` is the generated static site output. It is ignored by normal development and published by release automation.

If a bundler is introduced, it remains local to `web/` and keeps generated dependencies, caches, and final output out of the repository root.

## Build Flow

The website has a repository-root developer build entrypoint:

```bat
web-build.cmd
```

The website build:

1. Builds CaseDash through the project build entrypoint when generated screenshots or guide sheets are stale or missing.
2. Reads theme definitions from `resources/config.ini`.
3. Runs the built executable once per theme for dashboard screenshot generation.
4. Runs the built executable once per theme for layout guide sheet generation.
5. Writes theme metadata JSON.
6. Copies static website source and generated assets into `web/dist/`.

When the build needs screenshots or guide sheets, diagnostics output paths point under `build\` or `web\dist\` so generated diagnostics files do not pollute the repository root.

The generated site is directly openable from `web/dist/index.html` in a local browser. No preview server is required.

The release workflow publishes `web/dist/` to GitHub Pages at:

```text
https://casedash.github.io/casedash/
```

## Validation

Website validation checks:

- `web-build.cmd` completes from a clean checkout after the normal project build.
- The generated site opens directly from `web/dist/index.html` with no server-only assumptions.
- Navigation anchors scroll to all required sections.
- The theme switcher lists every `[theme.<name>]` section from `resources/config.ini`.
- Theme switching updates colors, screenshot, and layout guide sheet together.
- The page works at desktop, tablet, and phone widths.
- Text and buttons do not overlap or overflow at narrow widths.
- Images use explicit dimensions or aspect-ratio constraints to prevent layout shift.
- All important images have useful alt text.
- Links to releases, repository, issues, and docs resolve to the intended GitHub targets.

## Accessibility And Performance

The page is usable without JavaScript except for theme switching and theme-specific asset swapping. The default rendered HTML still shows one valid theme, one screenshot, and one layout guide sheet.

The page respects reduced-motion preferences. Smooth scrolling and theme transitions are disabled or shortened when the visitor prefers reduced motion.

Generated images are optimized for web delivery and include width and height metadata. The active theme's screenshot is loaded eagerly in the intro section; non-active theme images may load lazily.

The color derivation used by the website maintains readable contrast for body text, navigation text, buttons, and links for every generated app theme. If a theme cannot satisfy minimum contrast through derived colors, the build reports the theme and role that failed.

## Open Decisions

- Decide the exact GitHub Pages publication mechanism in the release workflow.
