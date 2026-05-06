# Util Package

`src/util/` owns pure shared utilities that are domain-neutral and safe for lower layers.

## Responsibilities

- Win32-backed `FilePath` and filesystem helpers.
- Command-line text handling after one Win32-to-UTF-8 conversion at process startup.
- String trimming, splitting, case folding, and whitespace normalization.
- Enum string conversion.
- UTF-8 and UTF-16 boundary conversion helpers.
- Embedded resource loading.
- Localization catalog access.
- Numeric safety helpers, DPI scale conversion, and domain-neutral numeric text formatting.
- Win32 HRESULT text formatting in the narrow Win32 formatting module.
- Trace line emission and trace value formatting built from domain-neutral formatters.
- Non-owning callback views such as `FunctionRef`.

## Boundaries

- `util` may depend on `util` only.
- It does not depend on config, telemetry, renderer, widget, UI, diagnostics, or application-facing packages.
- `strings.*` stays limited to domain-neutral string operations; config-language spelling, menu labels, telemetry labels, and UI copy live in the package that owns that contract.
- Project filesystem operations use `src/util/file_path.*` helpers instead of `std::filesystem` so path handling stays UTF-16 Win32-backed without pulling the standard filesystem library into the executable.
