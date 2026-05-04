## Documentation

Use the following maintained references and keep them in sync before finishing work:

- `docs/specifications.md` - general user-visible runtime behavior.
- `docs/hardware.md` - supported hardware-provider details, provider runtime requirements, provider-specific telemetry behavior, and provider troubleshooting.
- `docs/layout_edit.md` - layout-edit mode, edit-target interaction, modeless editor behavior, and layout-edit save or discard behavior.
- `docs/layout_guide_sheet.md` - diagnostics layout guide sheet feature behavior.
- `docs/diagnostics.md` - diagnostics flow, trace, dump, screenshot, app-icon, and validation recipes.
- `docs/architecture.md` and `docs/architecture/*.md` - code organization, subsystem boundaries, package notes, and build-graph structure.
- `docs/project.md` - documentation ownership, repository conventions, engineering constraints, and project pitfall notes.
- `docs/build.md` - project environment, setup, build commands, validation entrypoints, and tooling constraints.
- `docs/web.md` - public website behavior, section contract, content, build flow, and generated-asset contracts.
- `docs/profile_benchmark.md` - benchmark workflow, baselines, hotspots, tested hypotheses, and experiment results.

Keep a single source of truth for examples, format descriptions, and configuration-language references. Document requirements briefly in the owning spec using present-tense steady-state language.

Write minimal commit messages: one line starting with an action verb such as `Added`, `Changed`, `Refactored`, or `Removed`, followed by a short summary of the code or logic change. Mention docs only when docs are the explicit task.

## Build And Validation

- Use `build.cmd` for builds, then run validation commands sequentially against the fresh build.
- Keep build artifacts and temporary compiler files under `build\`.
- Use `format.cmd` for C++ formatting checks and fixes.
- Do not run `lint.cmd tidy` locally unless explicitly requested.
- Prefer headless verification commands after building; use the smallest `/exit` combination that proves the change.
- Add `/default-config` when validation is meant to exercise the built-in `resources/config.ini`.
- Put explicit diagnostics outputs under `build\`.
- Use `docs/diagnostics.md` for diagnostics command examples and inspection guidance.

## Project Constraints

- Ignore the stray `$null` file at the repository root; it is a Codex sandbox artifact.
- This project does not main backwards compatibility; do not preserve legacy config compatibility unless explicitly requested.
- Keep headers declarative; non-template and non-inline-required production logic belongs in `.cpp` files.
- Add a short code comment when a condition preserves a supported case or invariant that is not obvious nearby.
- If you resolve a project-specific pitfall, record it in the owning documentation file.
