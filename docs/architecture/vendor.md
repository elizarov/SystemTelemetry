# Vendor Package

`src/vendor/` contains narrow runtime vendored source kept inside the repository only when package-managed dependencies are not practical. `src/tools/vendor/` contains tool-only vendored source, including grammar inputs and generated parser sources used by native repository tools.

## Responsibilities

- Keep vendored code isolated from project-authored package layering.
- Avoid project-specific logic inside vendored files.
- Prefer package-managed dependencies for new third-party code when practical.

## Boundaries

- Project-authored code treats vendored source as external implementation detail.
- Formatting and lint eligibility filters keep vendored code separate from maintained project source checks unless a tool explicitly includes it.
