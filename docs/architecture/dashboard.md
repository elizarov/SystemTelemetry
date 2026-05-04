# Dashboard Package

`src/dashboard/` owns the interactive dashboard shell, controller, tray integration, menu flow, auto-start handling, service registration helpers, shell dialogs, and shared shell constants.

## Responsibilities

- `DashboardApp` owns HWND lifetime, message dispatch, tray integration, repaint invalidation, move-mode presentation, and the shell side of layout-edit interaction.
- `DashboardController` owns active config state, runtime instance lifetime, diagnostics session lifetime, save and reload actions, layout and scale switching, runtime target selection, and layout-edit session state.
- `DashboardShellUi` owns popup-menu construction, focused command dispatch, custom prompts, and the host bridge for the modeless editor window.
- Auto-start enablement writes the machine-wide Run entry for per-user dashboard UI startup and installs or starts the `CashDashService` LocalSystem service; disabling auto-start removes both.
- Configure-display and save flows use package-owned elevated helper routes when the current process cannot write target registry values, service registration, or executable-side files directly.

## Boundaries

- Dashboard composes lower-layer config, telemetry, renderer, diagnostics, display, layout-edit, and dialog services.
- Reusable config, telemetry, rendering, and layout-edit logic stays in the owning lower package instead of shell modules.
- Shell dialogs use Win32 dialog templates and control ids from `resources/CaseDash.rc` and `resources/resource.h`.
