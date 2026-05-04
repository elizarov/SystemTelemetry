# Util Package

`src/util/` owns pure shared utilities that are domain-neutral and safe for lower layers.

## Responsibilities

- Win32-backed `FilePath` and filesystem helpers.
- Command-line text handling.
- String trimming, splitting, case folding, and whitespace normalization.
- Enum string conversion.
- UTF-8 and UTF-16 boundary conversion helpers.
- Embedded resource loading.
- Localization catalog access.
- Numeric safety helpers and DPI scale conversion.
- Trace line emission and generic trace value formatting.
- Non-owning callback views such as `FunctionRef`.

## Boundaries

- `util` may depend on `util` only.
- It does not depend on config, telemetry, renderer, widget, UI, diagnostics, or application-facing packages.
- Project filesystem operations use `src/util/file_path.*` helpers instead of `std::filesystem` so path handling stays UTF-16 Win32-backed without pulling the standard filesystem library into the executable.
