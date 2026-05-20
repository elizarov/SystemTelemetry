# Util Package

`src/util/` owns pure shared utilities that are domain-neutral and safe for every package layer.

## Responsibilities

- UTF-8 `FilePath` and filesystem helpers that use Win32 A APIs by default and widen only for wide-native interfaces.
- Command-line text handling after the `CommandLineToArgvW` wide-native parser boundary at process startup.
- String trimming, splitting, case folding, and whitespace normalization.
- Enum string conversion.
- UTF-8 validation and narrow/wide boundary conversion helpers for APIs with no A-style text boundary.
- Embedded resource loading and generated text-atlas resource string hash-id lookup.
- Localization catalog access.
- Numeric safety helpers, DPI scale conversion, and domain-neutral numeric text formatting.
- Shared sprintf-style text formatting through `FormatText`, `AssignFormat`, and `AppendFormat`, including `ResourceStringId` overloads for resource-backed format strings.
- High-precision timer and trace-scoped timing aggregation helpers for low-overhead runtime profiling.
- Lightweight mutex locking for shared runtime state that needs a small platform-backed guard without exposing the native primitive in package APIs.
- Win32 HRESULT text formatting in the narrow Win32 formatting module.
- Trace prefix cataloging, filtering, line emission, resource-backed trace string resolution, and trace value formatting built from domain-neutral formatters.
- Non-owning callback views such as `FunctionRef`.

## Boundaries

- `util` may depend on `util` only.
- Every non-util package may depend on `util` for domain-neutral helpers.
- It does not depend on config, telemetry, renderer, widget, UI, diagnostics, or application-facing packages.
- `strings.*` stays limited to domain-neutral string operations; config-language spelling, menu labels, telemetry labels, and UI copy live in the package that owns that contract.
- Project filesystem operations use `src/util/file_path.*` helpers instead of `std::filesystem` so app-owned path data stays UTF-8 and filesystem calls use Win32 A APIs by default.
