# Dashboard Package

`src/dashboard/` owns the interactive dashboard shell, controller, tray integration, menu flow, auto-start handling, service registration helpers, shell dialogs, and shared shell constants.

## Responsibilities

- `DashboardApp` owns HWND lifetime, message dispatch, tray integration, repaint invalidation, move and resize placement presentation, and the shell side of layout-edit interaction.
- `DashboardApp` also owns the hover-activated titlebar state, including the invisible virtual-titlebar hover probe, monitor-fit checks, client-rect-preserving style changes, DWM native-chrome hints, native titlebar combo HWNDs, adaptive titlebar layout snapshots, shared titlebar control tooltip routing, shared selected-background color for the edit-layout button, and shared custom display or close button routing.
- `DashboardApp` applies display placement to dashboard client rectangles. Before live configure-display placement, it hides the hover titlebar frame when the target client rectangle cannot legally show that frame, so fullscreen and top-edge origins are not offset by stale native-caption margins.
- `DashboardTooltip` owns the shared Win32 tooltip window used by titlebar controls and layout-edit dashboard hover targets; layout-edit continues to own tooltip payload interpretation and text formatting.
- `DashboardController` owns active config state, runtime instance lifetime, diagnostics session lifetime, save and reload actions, layout and scale switching, runtime target selection, layout-edit session state, and one-shot config updates when move or resize placement completes.
- `DashboardShellUi` owns popup-menu construction, dashboard menu label formatting, generated bitmap icons attached to native configure-display menu rows, focused command dispatch, custom prompts, titlebar command forwarding, and the host bridge for the modeless editor window.
- Auto-start enablement installs or updates the `CashDashService` LocalSystem service, starts it, waits for `SERVICE_RUNNING`, then writes the machine-wide Run entry for per-user dashboard UI startup; if Run registration fails, the service change is rolled back. Disabling auto-start removes the Run entry, stops the service, requests deletion, and accepts SCM pending-delete as cleaned up.
- Configure-display and save flows use package-owned elevated helper routes when the current process cannot write target registry values, service registration, or executable-side files directly.

## Boundaries

- Dashboard composes lower-layer config, telemetry, renderer, diagnostics, display, layout-edit, and dialog services.
- Reusable config, telemetry, rendering, and layout-edit logic stays in the owning lower package instead of shell modules.
- Shell dialogs use Win32 dialog templates and control ids from `resources/CaseDash.rc` and `resources/resource.h`.
