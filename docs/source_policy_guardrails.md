# Source Policy Guardrails

This page summarizes hard project source-policy lessons. `lint.cmd` enforces the mechanical bans where practical; code review keeps the remaining rules when a source check would be noisy. These are not open research questions; new code follows these rules unless a future measured pass updates the rule and this page together.

- `std::function` is not used in maintained source. Use `FunctionRef` for synchronous borrowed callbacks or a purpose-built interface when callback ownership must outlive the call.
- `std::filesystem` and `<filesystem>` are not used. Use `src/util/file_path.*` so path handling stays Win32-backed without pulling filesystem machinery into the shipped app.
- STL threading primitives are not used in maintained source or tests. Use `LightweightMutex` for small locks and direct Win32 thread/event handles for worker wakeups instead of `std::mutex`, `std::thread`, or `std::condition_variable`.
- `std::hash` is not used for fixed project caches. Use a concrete project-owned helper such as `StableStringHash`, compact scans, fixed slots, or a measured package-owned custom hash table.
- Maintained source does not use conditional compilation guards. Target-specific helper code stays ordinary code and relies on the linker to remove unreferenced paths.
- Source literals stay narrow UTF-8 by default. Fixed UTF-16 literals are allowed only as documented `const wchar_t` boundary constants for Win32 or managed interop.
- Shared string formatting and common utility helpers are the default for new code. Use `Trace::WriteFmt` or `Trace::WriteLazyFmt` for trace lines, `FormatText`, `AssignFormat`, and `AppendFormat` from `src/util/text_format.*` for general narrow text, and existing domain helpers such as `AppendHresult`, config color formatters, and `src/util/file_path.*` before adding local builders. Avoid new `std::to_string` plus `operator+` chains, repeated append builders, and open-coded stack-buffer formatting unless a measured owner-specific helper is smaller or the text contract is package-specific.

