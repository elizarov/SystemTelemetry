# Website Specification

This document owns the public CaseDash website behavior, content, section contract, build flow, and generated-asset contracts. Keep it in sync with `web/index.html` whenever section ids, navigation labels, section order, or section content change.

## Purpose

The website is the end-user introduction to CaseDash. It presents what the app is, how to download it, what hardware providers are currently supported, how first use works, how layout editing looks, and where to contribute.

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

The maintained section ids, navigation labels, and order are:

- `download` - `Download`
- `features` - `Features`
- `hardware` - `Hardware`
- `layout` - `Layout`
- `contribute` - `Contribute`

The desktop navigation and mobile section picker expose the same section ids and labels. The page includes a compact footer for copyright, license text, and the license link; the footer is not a primary section in top navigation.

## Download Section

The download section is the first viewport signal and identifies the product as `CaseDash`.

The section includes:

- A short product description aligned with the README overview.
- Primary links for the latest release download and the GitHub repository.
- A dashboard screenshot for the active website theme.
- A short first-use path: install the MSI, run CaseDash, right-click, choose layout and theme, configure display, and enable startup when ready.

The download copy stays practical and avoids marketing-only language. The screenshot is the dominant visual object.

The top-left navigation brand shows `CaseDash` plus the current `VERSION` value as small muted `v<version>` text.

## Features Section

The features section summarizes the product capabilities that matter to end users:

- Compact always-on Windows telemetry dashboard.
- Constraint-based layouts and live layout editing.
- Theme system derived from a small set of base colors.
- Native executable under 1 MB and fast frame rendering.
- Layouts for common small-panel formats.
- Built-in display setup, placement restore, startup wallpaper, and Start with Windows support.

Each feature item uses short copy and, where useful, a small cropped screenshot or visual treatment generated from actual app output. The section does not restate the full configuration language.

## Hardware Section

The hardware section lists currently supported providers and telemetry families:

- AMD Radeon GPU telemetry through ADLX.
- Intel GPU telemetry through Level Zero Sysman.
- NVIDIA GPU telemetry through NVML.
- Presented-FPS capture through ETW via the CaseDash service.
- ASUS board temperature and fan telemetry through Armoury Crate ATKACPI.
- MSI board temperature and fan telemetry through MSI Center SDK.
- Gigabyte board temperature and fan telemetry through SIV.

The website keeps this section concise; [docs/hardware.md](hardware.md) owns provider requirements and troubleshooting details.

## Layout Section

The layout section introduces layout selection and layout editing from an end-user perspective.

The section includes:

- A short explanation that CaseDash uses named static layouts sized for small displays.
- The generated layout guide sheet for the active website theme.
- A concise explanation of what the guide sheet shows: editable cards, widget guides, spacing controls, and hover-style callouts.
- A link to `docs/layout.md` for the maintained layout language reference.

The page does not duplicate layout syntax examples unless the project later designates this website copy as the maintained source for a specific example.

## Contribute Section

The contribute section keeps detail light and sends visitors to GitHub.

It includes:

- The `Contributions` eyebrow.
- The heading `Bring your hardware and ideas`.
- Short copy inviting code, issues, sketches, ideas, themes, layouts, telemetry providers, localization, and practical feedback.
- Copy explaining that provider work needs owners with matching hardware, SDKs, and patience.
- Repository and Issues links.

## Design System

The website uses a minimal visual style that echoes CaseDash itself and stays consistent from section to section:

- High-contrast dashboard-like surfaces.
- Compact typography.
- Restrained separators and rounded card chrome.
- Minimal feature cards use plain panel surfaces and separators instead of decorative gradients.
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

The page favicon follows the browser `prefers-color-scheme` value. The dark favicon uses the `dark_cyan` app icon, and the light favicon uses the `blueprint_light` app icon, matching the README light and dark screenshot representatives.

## Generated Assets

The website source does not manually mirror screenshots, guide sheets, or theme metadata for each theme.

GitHub social preview artwork is generated separately by `tools\generate_social_preview.ps1`. The script writes a 1280 x 640 PNG under `build\social_preview\` by default, using the `dark_cyan` theme, fake telemetry, the built-in default config, a generated dashboard screenshot, and a generated app icon from the executable. The composition shows the app icon, white `CaseDash` wordmark, the motto `Compact Windows dashboard for dedicated PC screens`, the repository link, and the centered-right fake dashboard screenshot without feature-list boxes.

The website build generates, for every configured app theme:

- A dashboard screenshot using the default layout.
- A layout guide sheet using the default layout.
- An app icon used by the brand mark, theme switcher assets, and light/dark favicon links.
- A metadata entry with theme id, description, and the four base tokens.

Generated dashboard screenshots use:

- Built-in default config behavior.
- Fake telemetry.
- Default layout.
- Render scale `2`.
- The selected theme.

Generated layout guide sheets use the same config, fake/default data, scale, and selected theme. The guide sheet generation uses the diagnostics layout-guide-sheet output instead of a separate web-only renderer.

Generated screenshots, guide sheets, and website app icons are not committed. Generated files are written under the web build output so repository roots and documentation image folders stay clean. Generated dashboard screenshots and layout guide sheets are encoded as opaque 24-bit PNGs; generated app icons retain alpha and use compressed PNG output.

## Source Layout

Website source lives under `web/`.

Source layout:

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

1. Builds CaseDash through the project build entrypoint when generated website assets are stale or missing.
2. Reads theme definitions from `resources/config.ini`.
3. Runs the built executable once per theme to generate that theme's dashboard screenshot, layout guide sheet, and app icon when any of those target files is missing.
4. Writes theme metadata JSON.
5. Copies static website source and generated assets into `web/dist/`.

`web-build.cmd clean` removes `web/dist/` before the build and forces every generated screenshot, guide sheet, and app icon to be rebuilt.

The build prints one progress line per theme while generating or reusing that theme's dashboard screenshot, layout guide sheet, and app icon.

When the build needs generated app assets, diagnostics output paths point under `build\` or `web\dist\` so generated diagnostics files do not pollute the repository root.

The generated site is directly openable from `web/dist/index.html` in a local browser. No preview server is required.

The release workflow and the manual-only `Pages` workflow publish `web/dist/` to GitHub Pages through `actions/upload-pages-artifact` and `actions/deploy-pages` at:

```text
https://casedash.github.io/casedash/
```

## Maintenance And Validation

Website validation checks:

- `web-build.cmd` completes from a clean checkout after the normal project build.
- The generated site opens directly from `web/dist/index.html` with no server-only assumptions.
- Navigation anchors and the mobile section picker scroll to `download`, `features`, `hardware`, `layout`, and `contribute`.
- The theme switcher lists every `[theme.<name>]` section from `resources/config.ini`.
- Theme switching updates colors, screenshot, layout guide sheet, and app icon together.
- Browser light/dark scheme changes select the matching README-representative favicon.
- The page works at desktop, tablet, and phone widths.
- Text and buttons do not overlap or overflow at narrow widths.
- Images use explicit dimensions or aspect-ratio constraints to prevent layout shift.
- All important images have useful alt text.
- Links to releases, repository, issues, and docs resolve to the intended GitHub targets.
- Section ids, navigation labels, and section order in this file match `web/index.html`.

## Accessibility And Performance

The page is usable without JavaScript except for theme switching and theme-specific asset swapping. The default rendered HTML still shows one valid theme, one app icon, one screenshot, and one layout guide sheet.

The page respects reduced-motion preferences. Smooth scrolling and theme transitions are disabled or shortened when the visitor prefers reduced motion.

Generated images are optimized for web delivery, avoid alpha channels for opaque dashboard and guide-sheet assets, and include width and height metadata. The active theme's screenshot is loaded eagerly in the download section; non-active theme images may load lazily.

The color derivation used by the website maintains readable contrast for body text, navigation text, buttons, and links for every generated app theme. If a theme cannot satisfy minimum contrast through derived colors, the build reports the theme and role that failed.
