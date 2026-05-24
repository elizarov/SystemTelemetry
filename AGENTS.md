## Documentation

Use the following maintained references and keep them in sync before finishing work:

- `docs/specifications.md` - general user-visible runtime behavior.
- `docs/glossary.md` - canonical project terminology and context-specific spelling rules.
- `docs/hardware.md` - supported hardware-provider details, provider runtime requirements, provider-specific telemetry behavior, and provider troubleshooting.
- `docs/layout.md` and `docs/theme_configuration.md` - config language, section ownership, validation rules, and theme or derived color behavior.
- `docs/layout_edit.md` - layout-edit mode, edit-target interaction, modeless editor behavior, and layout-edit save or discard behavior.
- `docs/layout_guide_sheet.md` - diagnostics layout guide sheet feature behavior.
- `docs/diagnostics.md` - diagnostics flow, trace, snapshot dump, screenshot, app icon, and validation recipes.
- `docs/architecture.md` and `docs/architecture/*.md` - code organization, subsystem boundaries, package notes, and build-graph structure.
- `docs/project.md` - documentation ownership, repository conventions, engineering constraints, and project pitfall notes.
- `docs/build.md` - project environment, setup, build commands, validation entrypoints, and tooling constraints.
- `docs/format.md` - source formatting ownership, layout rules, spacing rules, wrapping rules, and include sorting behavior.
- `docs/release.md` and `docs/changelog.md` - release procedure, changelog format, and published release notes.
- `docs/web.md` - public website behavior, section contract, content, build flow, and generated-asset contracts.
- `docs/profile_benchmark.md` - benchmark workflow, shared optimization journal, tested hypotheses, and experiment results.
- `docs/performance/*.md` - machine-specific benchmark ranges, current bottlenecks, and further research directions.

Keep a single source of truth for terminology, examples, format descriptions, and configuration-language references. Document requirements briefly in the owning spec using present-tense steady-state language. Do not number section headings; use descriptive non-numbered headings so docs stay easier to reorder and maintain.

## Commit messages and pull requests

Write commit messages with a concise subject starting with an action verb. Good examples include `Added`, `Changed`, `Fixed`, `Improved`, `Refactored`, and `Removed`. For non-trivial changes, add a short body with tight bullets that summarize each meaningful change or improvement. Keep bullets factual, present-tense, and focused on behavior, structure, validation, or tooling outcomes rather than file-by-file narration. Mention docs only when docs are the explicit task. Pull requests must follow the same structure.

## Build And Validation

- Use `build.cmd` for builds, then run validation commands sequentially against the fresh build.
- Keep build artifacts and temporary compiler files under `build\`.
- Use `format.cmd` for C++ formatting checks and fixes.
- Prefer headless verification commands after building; use the smallest `/exit` combination that proves the change.
- Add `/default-config` when validation is meant to exercise the built-in `resources/config.ini`.
- Put explicit diagnostics outputs under `build\`.
- Use `docs/diagnostics.md` for diagnostics command examples and inspection guidance.

## Project Constraints

- Ignore the stray `$null` file at the repository root; it is a Codex sandbox artifact.
- This project does not maintain backward config compatibility unless explicitly requested.
- Keep headers declarative; non-template and non-inline-required production logic belongs in `.cpp` files.
- Add a short code comment when a condition preserves a supported case or invariant that is not obvious nearby.
- If you resolve a project-specific pitfall, record it in the owning documentation file.
