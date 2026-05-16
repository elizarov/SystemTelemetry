# Util Package

`src/util/` owns pure shared utilities that are domain-neutral and safe for every package layer.

## Responsibilities

- UTF-8 `FilePath` and filesystem helpers that widen only at Win32 filesystem calls.
- Command-line text handling after one Win32-to-UTF-8 conversion at process startup.
- String trimming, splitting, case folding, and whitespace normalization.
- Enum string conversion.
- UTF-8 and UTF-16 boundary conversion helpers.
- Embedded resource loading.
- Localization catalog access.
- Numeric safety helpers, DPI scale conversion, and domain-neutral numeric text formatting.
- High-precision timer and trace-scoped timing aggregation helpers for low-overhead runtime profiling.
- Lightweight mutex locking for shared runtime state that needs a small platform-backed guard without exposing the native primitive in package APIs.
- Win32 HRESULT text formatting in the narrow Win32 formatting module.
- Trace prefix cataloging, filtering, line emission, and trace value formatting built from domain-neutral formatters.
- Non-owning callback views such as `FunctionRef`.

## Boundaries

- `util` may depend on `util` only.
- Every non-util package may depend on `util` for domain-neutral helpers.
- It does not depend on config, telemetry, renderer, widget, UI, diagnostics, or application-facing packages.
- `strings.*` stays limited to domain-neutral string operations; config-language spelling, menu labels, telemetry labels, and UI copy live in the package that owns that contract.
- Project filesystem operations use `src/util/file_path.*` helpers instead of `std::filesystem` so app-owned path data stays UTF-8 while filesystem calls still use the Win32 wide API.
