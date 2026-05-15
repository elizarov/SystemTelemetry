# Source Policy Guardrails

This page summarizes hard project lessons that are enforced by `lint.cmd` source-policy checks. These are not open research questions; new code follows these rules unless a future measured pass updates the rule and this page together.

- `std::function` is not used in maintained source. Use `FunctionRef` for synchronous borrowed callbacks or a purpose-built interface when callback ownership must outlive the call.
- `std::filesystem` and `<filesystem>` are not used. Use `src/util/file_path.*` so path handling stays Win32-backed without pulling filesystem machinery into the shipped app.
- STL threading primitives are not used in maintained source or tests. Use `LightweightMutex` for small locks and direct Win32 thread/event handles for worker wakeups instead of `std::mutex`, `std::thread`, or `std::condition_variable`.
- `std::hash` is not used for fixed project caches. Use a concrete project-owned helper such as `StableStringHash`, compact scans, fixed slots, or a measured package-owned custom hash table.
- Maintained source does not use conditional compilation guards. Target-specific helper code stays ordinary code and relies on the linker to remove unreferenced paths.
- Source literals stay narrow UTF-8 by default. Fixed UTF-16 literals are allowed only as documented `const wchar_t` boundary constants for Win32 or managed interop.

